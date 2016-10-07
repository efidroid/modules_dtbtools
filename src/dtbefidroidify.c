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
#include <lib/boot.h>
#include <lib/boot/qcdt.h>

#define DTB_PAD_SIZE  1024
#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

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

/**
 * Delete a node in the fdt.
 *
 * @param blob      FDT blob to write into
 * @param node_name Name of node to delete
 * @return 0 on success, or -1 on failure
 */
static int delete_node(char *blob, const char *node_name)
{
    int node = 0;

    node = fdt_path_offset(blob, node_name);
    if (node < 0) {
        fprintf(stderr, "can't find %s: %s\n", node_name, fdt_strerror(node));
        return -1;
    }

    node = fdt_del_node(blob, node);
    if (node < 0) {
        fprintf(stderr, "can't delete %s: %s\n", node_name, fdt_strerror(node));
        return -1;
    }

    return 0;
}

static void generate_entries_add_cb(dt_entry_local_t *dt_entry, dt_entry_node_t *dt_list, const char *model)
{
    (void)(model);

    dt_entry_node_t *dt_node = dt_entry_list_alloc_node();
    memcpy(dt_node->dt_entry_m, dt_entry, sizeof(dt_entry_local_t));
    dt_entry_list_insert(dt_list, dt_node);
}

int process_dtb(const char *in_dtb, const char *outdir, uint32_t *countp, int remove_unused_nodes, const char *parser)
{
    int rc;
    off_t off;
    void *fdt = NULL;
    void *fdtcopy = NULL;
    ssize_t ssize;
    int offset_root;
    char buf[PATH_MAX];
    dt_entry_node_t *dt_list = NULL;

    printf("Processing %s\n", in_dtb);

    /* Initialize the dtb entry node*/
    dt_list = dt_entry_list_create();
    if (!dt_list) {
        fprintf(stderr, "Can't allocate dt list\n");
        return -ENOMEM;
    }

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

    // get chipinfo
    rc = libboot_qcdt_generate_entries(fdt, fdt_totalsize(fdt), dt_list, generate_entries_add_cb, parser);
    if (rc!=1) {
        fprintf(stderr, "can't get chipinfo: %d\n", rc);
        rc = -1;
        goto free_buffer;
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

    // recreate /chosen node to remove all it's contents
    delete_node(fdtcopy, "/chosen");
    fdt_add_subnode(fdtcopy, fdt_path_offset(fdtcopy, "/"), "chosen");

    // write new dtb's
    dt_entry_node_t *dt_node = NULL;
    dt_entry_data_t *dt_entry = NULL;
    libboot_list_for_every_entry(&dt_list->node, dt_node, dt_entry_node_t, node) {
        dt_entry = &dt_node->dt_entry_m->data;
        int fdout = -1;
        const char *parser_name = dt_node->dt_entry_m->parser;

        printf("chipset: %u, rev: %u, platform: %u, subtype: %u, pmic0: %u, pmic1: %u, pmic2: %u, pmic3: %u",
               dt_entry->platform_id, dt_entry->soc_rev, dt_entry->variant_id, dt_entry->board_hw_subtype,
               dt_entry->pmic_rev[0], dt_entry->pmic_rev[1], dt_entry->pmic_rev[2], dt_entry->pmic_rev[3]);

        if (!strcmp(parser_name, "qcom_lge")) {
            printf(", lgerev: %x", dt_entry->u.lge.lge_rev);
        }

        if (!strcmp(parser_name, "qcom_oppo")) {
            printf(", oppoid: %x/%x", dt_entry->u.oppo.id0, dt_entry->u.oppo.id1);
        }

        printf("\n");

        // patch msm-id
        if (dt_entry->version==1) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->platform_id);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->variant_id);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->soc_rev);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            if (!strcmp(parser_name, "qcom_lge")) {
                rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->u.lge.lge_rev);
                if (rc < 0) {
                    fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                    rc = -1;
                    goto next_chip;
                }
            }
        } else if (dt_entry->version==2 || dt_entry->version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->platform_id);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,msm-id", dt_entry->soc_rev);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }
        }

        // patch board-id
        if (dt_entry->version==2 || dt_entry->version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,board-id", dt_entry->variant_id);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,board-id", dt_entry->board_hw_subtype);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            if (!strcmp(parser_name, "qcom_oppo")) {
                rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,board-id", dt_entry->u.oppo.id0);
                if (rc < 0) {
                    fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                    rc = -1;
                    goto next_chip;
                }

                rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,board-id", dt_entry->u.oppo.id1);
                if (rc < 0) {
                    fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                    rc = -1;
                    goto next_chip;
                }
            }
        }

        // patch pmic-id
        if (dt_entry->version==3) {
            // get root
            offset_root = fdt_path_offset(fdtcopy, "/");
            if (offset_root<0) {
                fprintf(stderr, "Can't get root node %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_setprop_u32(fdtcopy, offset_root, "qcom,pmic-id", dt_entry->pmic_rev[0]);
            if (rc < 0) {
                fprintf(stderr, "Can't set property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", dt_entry->pmic_rev[1]);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", dt_entry->pmic_rev[2]);
            if (rc < 0) {
                fprintf(stderr, "Can't append property %s\n", fdt_strerror(offset_root));
                rc = -1;
                goto next_chip;
            }

            rc = fdt_appendprop_u32(fdtcopy, offset_root, "qcom,pmic-id", dt_entry->pmic_rev[3]);
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
        rc = fdt_setprop(fdtcopy, offset_root, "efidroid-soc-info", dt_entry, sizeof(*dt_entry));
        if (rc<0) {
            fprintf(stderr, "Can't set efidroid prop %s\n", fdt_strerror(rc));
            rc = -1;
            goto next_chip;
        }

        // write parser name
        rc = fdt_setprop_string(fdtcopy, offset_root, "efidroid-fdt-parser", parser_name);
        if (rc<0) {
            fprintf(stderr, "Can't set efidroid prop %s\n", fdt_strerror(rc));
            rc = -1;
            goto next_chip;
        }

        // build path
        ssize = snprintf(buf, sizeof(buf), "%s/%u.dtb", outdir, (*countp)++);
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
    if (argc!=5) {
        fprintf(stderr, "Usage: %s [in.dtb|indir] outdir remove_unused_nodes parser\n", argv[0]);
        return -EINVAL;
    }
    const char *indir = argv[1];
    const char *outdir = argv[2];
    int remove_unused_nodes = !strcmp(argv[3], "1");
    const char *parser = argv[4];

    libboot_init();

    // check directory
    if (!is_directory(outdir)) {
        fprintf(stderr, "'%s' is not a directory\n", outdir);
        return -EINVAL;
    }

    // check directory
    if (!is_directory(indir)) {
        return process_dtb(indir, outdir, &i, remove_unused_nodes, parser);
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

                rc = process_dtb(filename, outdir, &i, remove_unused_nodes, parser);
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
