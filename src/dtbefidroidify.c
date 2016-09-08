/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <libfdt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <list.h>

#define DTB_PAD_SIZE  1024
#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

typedef struct {
    list_node_t node;

    char *name;
    void *data;
    size_t size;
} fdtprop_t;

typedef struct {
    list_node_t node;

    char *name;
} fdtnode_t;

struct chipInfo_t {
    uint32_t version;
    uint32_t chipset;
    uint32_t platform;
    uint32_t subtype;
    uint32_t revNum;
    uint32_t isLge;
    uint32_t lgeRev;
    uint32_t pmic_model[4];
    uint32_t numId;
    uint32_t numSt;
    uint32_t numPt;
    struct chipInfo_t *master;
    struct chipInfo_t *t_next;
};
typedef struct chipInfo_t chipinfo_t;

struct chipId_t {
    uint32_t chipset;
    uint32_t revNum;
    uint32_t isLge;
    uint32_t lgeRev;
    uint32_t num;
    struct chipId_t *t_next;
};
typedef struct chipId_t chipid_t;

struct chipSt_t {
    uint32_t platform;
    uint32_t subtype;
    uint32_t num;
    struct chipSt_t *t_next;
};
typedef struct chipSt_t chipst_t;

struct chipPt_t {
    uint32_t pmic0;
    uint32_t pmic1;
    uint32_t pmic2;
    uint32_t pmic3;
    uint32_t num;
    struct chipPt_t *t_next;
};
typedef struct chipPt_t chippt_t;

typedef struct {
    uint32_t version;
    uint32_t chipset;
    uint32_t platform;
    uint32_t subtype;
    uint32_t revNum;
    uint32_t pmic_model[4];
} efidroid_fdtinfo_t;

off_t fdsize(int fd)
{
    off_t off;

    off = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0L, SEEK_SET);

    return off;
}

int is_directory(const char *path)
{
    struct stat path_stat;
    int rc = stat(path, &path_stat);
    if (rc) return -ENOENT;
    return S_ISDIR(path_stat.st_mode);
}

#define MAX_LEVEL   32      /* how deeply nested we will go */
int list_subnodes_callback(void *blob, const char *parentpath, int (*callback)(void *fdt, const char *path, void *pdata), void *pdata)
{
    int nextoffset;     /* next node offset from libfdt */
    uint32_t tag;       /* current tag */
    int level = 0;      /* keep track of nesting level */
    const char *pathp;
    int depth = 1;      /* the assumed depth of this node */
    const char *newpath = NULL;

    // get offset
    int node = fdt_path_offset(blob, parentpath);
    if (node < 0) {
        return 1;
    }

    if (!strcmp(parentpath, "/"))
        parentpath++;

    while (level >= 0) {
        tag = fdt_next_tag(blob, node, &nextoffset);
        switch (tag) {
            case FDT_BEGIN_NODE:
                pathp = fdt_get_name(blob, node, NULL);
                if (level <= depth) {
                    if (pathp == NULL)
                        pathp = "/* NULL pointer error */";
                    if (*pathp == '\0')
                        pathp = "/";    /* root is nameless */

                    if (level == 1) {
                        newpath = pathp;
                    }
                }
                level++;
                if (level >= MAX_LEVEL) {
                    printf("Nested too deep, aborting.\n");
                    return 1;
                }
                break;
            case FDT_END_NODE:
                level--;
                if (level == 0)
                    level = -1;     /* exit the loop */

                if (newpath && level==1) {
                    // allocate name
                    size_t nodepath_len = strlen(parentpath)+1+strlen(newpath)+1;
                    char *nodepath = malloc(nodepath_len);
                    if (!nodepath) return 1;

                    // build name
                    int rc = snprintf(nodepath, nodepath_len, "%s/%s", parentpath, newpath);
                    if (rc<0 || (size_t)rc>=nodepath_len)
                        return 1;

                    // callback
                    if (callback(blob, nodepath, pdata))
                        return 1;

                    // list subnodes
                    if (list_subnodes_callback(blob, nodepath, callback, pdata))
                        return 1;

                    // cleanup
                    free(nodepath);

                    newpath = NULL;
                }

                break;
            case FDT_END:
                return 1;
            case FDT_PROP:
                break;
            case FDT_NOP:
                break;
            default:
                if (level <= depth)
                    printf("Unknown tag 0x%08X\n", tag);
                return 1;
        }
        node = nextoffset;
    }
    return 0;
}

int fdt_get_qc_version(void *fdt)
{
    int len;
    int version = 1;

    int offset_root = fdt_path_offset(fdt, "/");
    if (offset_root<0) {
        return -1;
    }

    if (!fdt_get_property(fdt, offset_root, "qcom,msm-id", &len))
        return -1;

    if (fdt_get_property(fdt, offset_root, "qcom,board-id", &len))
        version = 2;

    if (fdt_get_property(fdt, offset_root, "qcom,pmic-id", &len))
        version = 3;

    return version;
}

chipinfo_t *fdt_get_qc_chipinfo(void *fdt, int version)
{
    int rc = 0;
    uint32_t i;
    int len_msm;
    int len_board;
    int len_pmic;
    const struct fdt_property *prop_msm;
    const struct fdt_property *prop_board;
    const struct fdt_property *prop_pmic;
    chipinfo_t *chip = NULL;
    chipid_t *chipid = NULL;
    chipst_t *chipst = NULL;
    chippt_t *chippt = NULL;
    uint32_t count1 = 0, count2 = 0, count3 = 0;

    int offset_root = fdt_path_offset(fdt, "/");
    if (offset_root<0) {
        fprintf(stderr, "root offset not found\n");
        return NULL;
    }

    prop_msm = fdt_get_property(fdt, offset_root, "qcom,msm-id", &len_msm);
    prop_board = fdt_get_property(fdt, offset_root, "qcom,board-id", &len_board);
    prop_pmic = fdt_get_property(fdt, offset_root, "qcom,pmic-id", &len_pmic);

    if (!prop_msm) {
        fprintf(stderr, "msm prop not found\n");
        return NULL;
    }

    if (version==1) {
        uint32_t *msmarr = (uint32_t *)prop_msm->data;

        if (len_msm%(sizeof(uint32_t)*3) == 0) {
            for (i=0; i<len_msm/sizeof(uint32_t); i+=3) {
                uint32_t chipset = fdt32_to_cpu(msmarr[i+0]);
                uint32_t platform = fdt32_to_cpu(msmarr[i+1]);
                uint32_t revNum = fdt32_to_cpu(msmarr[i+2]);

                chipinfo_t *tmp = malloc(sizeof(chipinfo_t));
                if (!tmp) {
                    rc = -ENOMEM;
                    goto cleanup;
                }
                if (!chip) {
                    chip = tmp;
                    chip->t_next = NULL;
                } else {
                    tmp->t_next = chip->t_next;
                    chip->t_next = tmp;
                }
                tmp->version  = version;
                tmp->chipset  = chipset;
                tmp->platform = platform;
                tmp->subtype  = 0;
                tmp->revNum   = revNum;
                tmp->isLge    = 0;
                tmp->pmic_model[0] = 0;
                tmp->pmic_model[1] = 0;
                tmp->pmic_model[2] = 0;
                tmp->pmic_model[3] = 0;
                tmp->numId = 3;
                tmp->numSt = 0;
                tmp->numPt = 0;
                count1++;
            }
        }

        // LGE format
        else if (len_msm%(sizeof(uint32_t)*4) == 0) {
            for (i=0; i<len_msm/sizeof(uint32_t); i+=4) {
                uint32_t chipset = fdt32_to_cpu(msmarr[i+0]);
                uint32_t platform = fdt32_to_cpu(msmarr[i+1]);
                uint32_t revNum = fdt32_to_cpu(msmarr[i+2]);
                uint32_t lgeRev = fdt32_to_cpu(msmarr[i+3]);

                chipinfo_t *tmp = malloc(sizeof(chipinfo_t));
                if (!tmp) {
                    rc = -ENOMEM;
                    goto cleanup;
                }
                if (!chip) {
                    chip = tmp;
                    chip->t_next = NULL;
                } else {
                    tmp->t_next = chip->t_next;
                    chip->t_next = tmp;
                }
                tmp->version  = version;
                tmp->chipset  = chipset;
                tmp->platform = platform;
                tmp->subtype  = 0;
                tmp->revNum   = revNum;
                tmp->lgeRev   = lgeRev;
                tmp->isLge    = 1;
                tmp->pmic_model[0] = 0;
                tmp->pmic_model[1] = 0;
                tmp->pmic_model[2] = 0;
                tmp->pmic_model[3] = 0;
                tmp->numId = 4;
                tmp->numSt = 0;
                tmp->numPt = 0;
                count1++;
            }
        }

        else {
            fprintf(stderr, "unknown msm-id format for V1 dtb\n");
            return NULL;
        }

        return chip;
    }

    else if (version==2 || version==3) {
        if (!prop_board) {
            fprintf(stderr, "board prop not found\n");
            return NULL;
        }
        if (version==3 && !prop_pmic) {
            fprintf(stderr, "pmic prop not found\n");
            return NULL;
        }
        if (len_msm%(sizeof(uint32_t)*2)) {
            fprintf(stderr, "msm prop has invalid length\n");
            return NULL;
        }

        uint32_t *msmarr = (uint32_t *)prop_msm->data;
        for (i=0; i<len_msm/sizeof(uint32_t); i+=2) {
            uint32_t chipset = fdt32_to_cpu(msmarr[i+0]);
            uint32_t revNum = fdt32_to_cpu(msmarr[i+1]);

            chipid_t *tmp_id = malloc(sizeof(chipid_t));
            if (!tmp_id) {
                rc = -ENOMEM;
                goto cleanup;
            }
            if (!chipid) {
                chipid = tmp_id;
                chipid->t_next = NULL;
            } else {
                tmp_id->t_next = chipid->t_next;
                chipid->t_next = tmp_id;
            }
            tmp_id->chipset = chipset;
            tmp_id->revNum = revNum;
            tmp_id->isLge = 0;
            tmp_id->num = 2;
            count1++;
        }

        uint32_t *boardarr = (uint32_t *)prop_board->data;
        for (i=0; i<len_board/sizeof(uint32_t); i+=2) {
            uint32_t platform = fdt32_to_cpu(boardarr[i+0]);
            uint32_t subtype = fdt32_to_cpu(boardarr[i+1]);

            chipst_t *tmp_st = malloc(sizeof(chipst_t));
            if (!tmp_st) {
                rc = -ENOMEM;
                goto cleanup;
            }
            if (!chipst) {
                chipst = tmp_st;
                chipst->t_next = NULL;
            } else {
                tmp_st->t_next = chipst->t_next;
                chipst->t_next = tmp_st;
            }

            tmp_st->platform = platform;
            tmp_st->subtype = subtype;
            tmp_st->num = 2;
            count2++;
        }

        if (prop_pmic) {
            uint32_t *pmicarr = (uint32_t *)prop_pmic->data;
            for (i=0; i<len_pmic/sizeof(uint32_t); i+=4) {
                uint32_t pmic0 = fdt32_to_cpu(pmicarr[i+0]);
                uint32_t pmic1 = fdt32_to_cpu(pmicarr[i+1]);
                uint32_t pmic2 = fdt32_to_cpu(pmicarr[i+2]);
                uint32_t pmic3 = fdt32_to_cpu(pmicarr[i+3]);

                chippt_t *tmp_pt = malloc(sizeof(chippt_t));
                if (!tmp_pt) {
                    rc = -ENOMEM;
                    goto cleanup;
                }
                if (!chippt) {
                    chippt = tmp_pt;
                    chippt->t_next = NULL;
                } else {
                    tmp_pt->t_next = chippt->t_next;
                    chippt->t_next = tmp_pt;
                }

                tmp_pt->pmic0 = pmic0;
                tmp_pt->pmic1 = pmic1;
                tmp_pt->pmic2 = pmic2;
                tmp_pt->pmic3 = pmic3;
                tmp_pt->num = 4;
                count3++;
            }
        }

        if (count1==0) {
            rc = -ENOENT;
            goto cleanup;
        }
        if (count2==0) {
            rc = -ENOENT;
            goto cleanup;
        }
        if (version==3 && count3==0) {
            rc = -ENOENT;
            goto cleanup;
        }

        chipid_t *cId = chipid;
        chipst_t *cSt = chipst;
        chippt_t *cPt = chippt;
        while (cId != NULL) {
            while (cSt != NULL) {
                if (version == 3) {
                    while (cPt != NULL) {
                        chipinfo_t *tmp = malloc(sizeof(chipinfo_t));
                        if (!tmp) {
                            rc = -ENOMEM;
                            goto cleanup;
                        }
                        if (!chip) {
                            chip = tmp;
                            chip->t_next = NULL;
                        } else {
                            tmp->t_next = chip->t_next;
                            chip->t_next = tmp;
                        }

                        tmp->version  = version;
                        tmp->chipset  = cId->chipset;
                        tmp->platform = cSt->platform;
                        tmp->revNum   = cId->revNum;
                        tmp->subtype  = cSt->subtype;
                        tmp->isLge    = cId->isLge;
                        tmp->lgeRev   = cId->lgeRev;
                        tmp->pmic_model[0] = cPt->pmic0;
                        tmp->pmic_model[1] = cPt->pmic1;
                        tmp->pmic_model[2] = cPt->pmic2;
                        tmp->pmic_model[3] = cPt->pmic3;
                        tmp->numId = cId->num;
                        tmp->numSt = cSt->num;
                        tmp->numPt = cPt->num;
                        cPt = cPt->t_next;
                    }
                    cPt = chippt;
                } else {
                    chipinfo_t *tmp = malloc(sizeof(chipinfo_t));
                    if (!tmp) {
                        rc = -ENOMEM;
                        goto cleanup;
                    }
                    if (!chip) {
                        chip = tmp;
                        chip->t_next = NULL;
                    } else {
                        tmp->t_next = chip->t_next;
                        chip->t_next = tmp;
                    }
                    tmp->version  = version;
                    tmp->chipset  = cId->chipset;
                    tmp->platform = cSt->platform;
                    tmp->revNum   = cId->revNum;
                    tmp->subtype  = cSt->subtype;
                    tmp->isLge    = cId->isLge;
                    tmp->lgeRev   = cId->lgeRev;
                    tmp->pmic_model[0] = 0;
                    tmp->pmic_model[1] = 0;
                    tmp->pmic_model[2] = 0;
                    tmp->pmic_model[3] = 0;
                    tmp->numId = cId->num;
                    tmp->numSt = cSt->num;
                    tmp->numPt = 0;
                }
                cSt = cSt->t_next;
            }
            cSt = chipst;
            cId = cId->t_next;
        }
    }

cleanup:
    // cleanup
    while (chipid) {
        chipid_t *tmp = chipid;
        chipid = chipid->t_next;
        free(tmp);
    }
    while (chipst) {
        chipst_t *tmp= chipst;
        chipst = chipst->t_next;
        free(tmp);
    }
    while (chippt) {
        chippt_t *tmp= chippt;
        chippt = chippt->t_next;
        free(tmp);
    }

    if (!rc) {
        return chip;
    }

    // cleanup
    while (chip) {
        chipinfo_t *tmp = chip;
        chip = chip->t_next;
        free(tmp);
    }
    return NULL;
}

int startswith(const char *str, const char *pre)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

static const char *whitelist[] = {
    "/aliases",
    "/chosen",
    "/memory",
    "/cpus",
    "/soc/qcom,mdss_mdp",
    "/soc/qcom,mdss_dsi",
    NULL,
};

int callback_fn(void *fdt, const char *path, void *fdtcopy)
{
    (void)(fdt);

    // get offset
    int offset = fdt_path_offset(fdtcopy, path);
    if (offset < 0) {
        // ignore this because it can happen when you remove nodes
        if (offset==-FDT_ERR_NOTFOUND)
            return 0;

        fprintf(stderr, "can't find node %s: %s\n", path, fdt_strerror(offset));
        return 1;
    }

    // scan whitelist
    int is_whitelisted = 0;
    const char **ptr = whitelist;
    while (*ptr) {
        // prefix is whitelisted
        if (startswith(path, *ptr)) {
            is_whitelisted = 1;
            break;
        }

        // this is the parent of a whitelisted item
        if (startswith(*ptr, path)) {
            is_whitelisted = 1;
            break;
        }

        ptr++;
    }

    // keep
    if (is_whitelisted)
        return 0;

    // remove
    int rc = fdt_del_node(fdtcopy, offset);
    if (rc < 0) {
        fprintf(stderr, "can't remove node %s: %s\n", path, fdt_strerror(rc));
        return 1;
    }

    return 0;
}

int process_dtb(const char *in_dtb, const char *outdir, uint32_t *countp, int remove_unused_nodes)
{
    int rc;
    off_t off;
    void *fdt = NULL;
    void *fdtcopy = NULL;
    ssize_t ssize;
    int offset_root;
    char buf[PATH_MAX];

    printf("Processing %s\n", in_dtb);

    // open file
    const char *filename = in_dtb;
    int fd = open(filename, O_RDONLY);
    if (fd<0) {
        fprintf(stderr, "Can't open file %s\n", filename);
        return fd;
    }

    // get filesize
    off = fdsize(fd);
    if (off<0) {
        fprintf(stderr, "Can't get size of file %s\n", filename);
        rc = (int)off;
        goto close_file;
    }

    // allocate buffer
    fdt = malloc(off);
    if (!fdt) {
        fprintf(stderr, "Can't allocate buffer of size %lu\n", off);
        rc = -ENOMEM;
        goto close_file;
    }

    // read file into memory
    ssize = read(fd, fdt, off);
    if (ssize!=off) {
        fprintf(stderr, "Can't read file %s into buffer\n", filename);
        rc = (int)ssize;
        goto free_buffer;
    }

    // check header
    if (fdt_check_header(fdt)) {
        fprintf(stderr, "Invalid fdt header\n");
        rc = -1;
        goto free_buffer;
    }

    // get qcdt version
    int version = fdt_get_qc_version(fdt);
    if (version<0) {
        fprintf(stderr, "can't get qcfdt version\n");
        goto free_buffer;
    }
    printf("version: %d\n", version);

    // get chipinfo
    chipinfo_t *chip = fdt_get_qc_chipinfo(fdt, version);
    if (!chip) {
        fprintf(stderr, "can't get chipinfo\n");
        rc = -ENOMEM;
        goto next_chip;
    }

    // allocate fdcopy
    size_t fdtcopysz = ROUNDUP(off + DTB_PAD_SIZE, sizeof(uint32_t));
    fdtcopy = malloc(fdtcopysz);
    if (!fdtcopy) {
        fprintf(stderr, "can't allocate fdtcopy\n");
        rc = -ENOMEM;
        goto next_chip;
    }

    // copy fdt
    rc = fdt_open_into(fdt, fdtcopy, fdtcopysz);
    if (rc<0) {
        fprintf(stderr, "can't copy fdt %s\n", fdt_strerror(rc));
        goto next_chip;
    }

    // remove unneeded nodes
    if (remove_unused_nodes) {
        list_subnodes_callback(fdt, "/", callback_fn, fdtcopy);
    }

    // write new dtb's
    chipinfo_t *t_chip;
    for (t_chip = chip; t_chip; t_chip = t_chip->t_next,(*countp)++) {
        int fdout = -1;

        printf("chipset: %u, rev: %u, platform: %u, subtype: %u, pmic0: %u, pmic1: %u, pmic2: %u, pmic3: %u\n",
               t_chip->chipset, t_chip->revNum, t_chip->platform, t_chip->subtype,
               t_chip->pmic_model[0], t_chip->pmic_model[1], t_chip->pmic_model[2], t_chip->pmic_model[3]);

        // create efidroid socinfo
        efidroid_fdtinfo_t fdtinfo = {
            .version = cpu_to_fdt32(version),
            .chipset = cpu_to_fdt32(t_chip->chipset),
            .platform = cpu_to_fdt32(t_chip->platform),
            .subtype = cpu_to_fdt32(t_chip->subtype),
            .revNum = cpu_to_fdt32(t_chip->revNum),
            .pmic_model = {
                cpu_to_fdt32(t_chip->pmic_model[0]),
                cpu_to_fdt32(t_chip->pmic_model[1]),
                cpu_to_fdt32(t_chip->pmic_model[2]),
                cpu_to_fdt32(t_chip->pmic_model[3]),
            },
        };

        // patch msm-id
        if (version==1) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->chipset);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->platform);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->revNum);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            // LGE
            if (t_chip->numId==4 && t_chip->isLge) {
                rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->lgeRev);
                if (rc < 0) {
                    fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                    rc = -1;
                    goto next_chip;
                }
            }
        } else if (version==2 || version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->chipset);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", t_chip->revNum);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }
        }

        // patch board-id
        if (version==2 || version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,board-id", t_chip->platform);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,board-id", t_chip->subtype);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }
        }

        // patch pmic-id
        if (version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,pmic-id", t_chip->pmic_model[0]);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", t_chip->pmic_model[1]);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", t_chip->pmic_model[2]);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", t_chip->pmic_model[3]);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }
        }

        // get root node
        offset_root = fdt_path_offset(fdtcopy, "/");
        if (offset_root<0) {
            fprintf(stderr, "can't get root offset %s\n", fdt_strerror(rc));
            rc = offset_root;
            goto next_chip;
        }

        // write efidroid soc info
        rc = fdt_setprop(fdtcopy, offset_root, "efidroid-soc-info", &fdtinfo, sizeof(fdtinfo));
        if (rc<0) {
            fprintf(stderr, "Can't set efidroid prop %s\n", fdt_strerror(rc));
            rc = -1;
            goto next_chip;
        }

        // build path
        ssize = snprintf(buf, sizeof(buf), "%s/%u.dtb", outdir, *countp);
        if (ssize<0 || (size_t)ssize>=sizeof(buf)) {
            fprintf(stderr, "Can't build filepath %ld\n", ssize);
            rc = -1;
            goto next_chip;
        }

        // open new dtb file
        fdout = open(buf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fdout<0) {
            fprintf(stderr, "Can't open file %s\n", buf);
            rc = fdout;
            goto next_chip;
        }

        // pack fdt
        fdt_pack(fdtcopy);

        // align fdt size
        rc = fdt_open_into(fdtcopy, fdtcopy, ROUNDUP(fdt_totalsize(fdtcopy), sizeof(uint32_t)));
        if (rc<0) {
            fprintf(stderr, "can't align fdt size %s\n", fdt_strerror(rc));
            goto next_chip;
        }

        // write new fdt
        ssize_t fdtsz = fdt_totalsize(fdtcopy);
        ssize = write(fdout, fdtcopy, fdtsz);
        if (ssize!=fdtsz) {
            fprintf(stderr, "Can't write fdt to file %s\n", buf);
            rc = (int)ssize;
            goto next_chip;
        }

        rc = 0;

next_chip:
        // close file
        if (fdout>=0)
            close(fdout);

        // cancel on error
        if (rc) {
            goto free_buffer;
        }
    }

    rc = 0;

free_buffer:
    free(fdt);
    free(fdtcopy);

close_file:
    // close file
    if (close(fd)) {
        fprintf(stderr, "Can't close file %s\n", filename);
        return rc;
    }

    if (rc) {
        fprintf(stderr, "ERROR: %s\n", strerror(-rc));
        return rc;
    }

    return rc;
}

int main(int argc, char **argv)
{
    uint32_t i = 0;
    int rc = 0;
    struct dirent *dp;
    char *filename = NULL;

    // validate arguments
    if (argc!=4) {
        fprintf(stderr, "Usage: %s in.dtb outdir remove_unused_nodes\n", argv[0]);
        return -EINVAL;
    }
    const char *indir = argv[1];
    const char *outdir = argv[2];
    int remove_unused_nodes = !strcmp(argv[3], "1");

    // check directory
    if (!is_directory(outdir)) {
        fprintf(stderr, "'%s' is not a directory\n", outdir);
        return -EINVAL;
    }

    // check directory
    if (!is_directory(indir)) {
        return process_dtb(indir, outdir, &i, remove_unused_nodes);
    }

    DIR *dir = opendir(indir);
    if (!dir) {
        fprintf(stderr, "Failed to open input directory '%s'\n", indir);
        return -1;
    }

    while ((dp = readdir(dir)) != NULL) {
        if (dp->d_type == DT_UNKNOWN) {
            struct stat statbuf;
            char name[PATH_MAX];
            snprintf(name, sizeof(name), "%s%s%s",
                     indir,
                     (indir[strlen(indir) - 1] == '/' ? "" : "/"),
                     dp->d_name);
            if (!stat(name, &statbuf)) {
                if (S_ISREG(statbuf.st_mode)) {
                    dp->d_type = DT_REG;
                } else if (S_ISDIR(statbuf.st_mode)) {
                    dp->d_type = DT_DIR;
                }
            }
        }

        if (dp->d_type == DT_REG) {
            int flen = strlen(dp->d_name);
            if ((flen > 4) && (strncmp(&dp->d_name[flen-4], ".dtb", 4) == 0)) {
                flen = strlen(indir) + 1 + strlen(dp->d_name) + 1;
                filename = (char *)malloc(flen);
                if (!filename) {
                    fprintf(stderr, "Out of memory\n");
                    rc = -ENOMEM;
                    break;
                }
                strncpy(filename, indir, flen);
                strncat(filename, "/", 1);
                strncat(filename, dp->d_name, flen);

                rc = process_dtb(filename, outdir, &i, remove_unused_nodes);
                if (rc) break;

                free(filename);
                filename = NULL;
            }
        }
    }
    closedir(dir);
    free(filename);

    return rc;
}
