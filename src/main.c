#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include <dev_tree.h>
#include <libfdt.h>

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))

#define PAGE_SIZE_DEF  2048
static int m_page_size = PAGE_SIZE_DEF;

off_t fdsize(int fd) {
    off_t off;

    off = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0L, SEEK_SET);

    return off;
}

off_t fdpos(int fd) {
    return lseek(fd, 0L, SEEK_CUR);
}

/* Returns 0 if the device tree is valid. */
int dev_tree_validate(struct dt_table *table, unsigned int page_size, uint32_t *dt_hdr_size)
{
	int dt_entry_size;
	uint64_t hdr_size;

	/* Validate the device tree table header */
	if(table->magic != DEV_TREE_MAGIC) {
		fprintf(stderr, "Bad magic in device tree table \n");
		return -1;
	}

	if (table->version == DEV_TREE_VERSION_V1) {
		dt_entry_size = sizeof(struct dt_entry_v1);
	} else if (table->version == DEV_TREE_VERSION_V2) {
		dt_entry_size = sizeof(struct dt_entry_v2);
	} else if (table->version == DEV_TREE_VERSION_V3) {
		dt_entry_size = sizeof(struct dt_entry);
	} else {
		fprintf(stderr, "Unsupported version (%d) in DT table \n",
				table->version);
		return -1;
	}

	hdr_size = (uint64_t)table->num_entries * dt_entry_size + DEV_TREE_HEADER_SIZE;

	/* Roundup to page_size. */
	hdr_size = ROUNDUP(hdr_size, page_size);

	if (hdr_size > UINT_MAX)
		return -1;
	else
		*dt_hdr_size = hdr_size & UINT_MAX;

	return 0;
}

int dev_tree_generate(const char* filename, struct dt_table *table) {
	uint32_t i;
    int rc;
	unsigned char *table_ptr = NULL;
    struct dt_entry dt_entry_buf_1;
	struct dt_entry_v1 *dt_entry_v1 = NULL;
	struct dt_entry_v2 *dt_entry_v2 = NULL;
    struct dt_entry *cur_dt_entry = NULL;
    struct dt_entry_v1 new_dt_entry_v1;
    struct dt_entry_v2 new_dt_entry_v2;
    struct dt_entry    new_dt_entry_v3;
    uint8_t fdt[4096];

    table_ptr = (unsigned char *)table + DEV_TREE_HEADER_SIZE;
	cur_dt_entry = &dt_entry_buf_1;

    int fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(fd<0) {
        fprintf(stderr, "Can't open file %s\n", filename);
        return fd;
    }

    // seek past header
    //lseek(fd, sizeof(struct dt_table), SEEK_SET);
    write(fd, table, sizeof(*table));

	fprintf(stdout, "DTB Total entry: %d, DTB version: %d\n", table->num_entries, table->version);
	for(i = 0; i < table->num_entries; i++) {
        switch(table->version) {
		case DEV_TREE_VERSION_V1:
			dt_entry_v1 = (struct dt_entry_v1 *)table_ptr;
            memcpy(&new_dt_entry_v1, dt_entry_v1, sizeof(new_dt_entry_v1));
			cur_dt_entry->platform_id = dt_entry_v1->platform_id;
			cur_dt_entry->variant_id = dt_entry_v1->variant_id;
			cur_dt_entry->soc_rev = dt_entry_v1->soc_rev;
			cur_dt_entry->board_hw_subtype = (dt_entry_v1->variant_id >> 0x18);
			/*cur_dt_entry->pmic_rev[0] = board_pmic_target(0);
			cur_dt_entry->pmic_rev[1] = board_pmic_target(1);
			cur_dt_entry->pmic_rev[2] = board_pmic_target(2);
			cur_dt_entry->pmic_rev[3] = board_pmic_target(3);*/
			cur_dt_entry->offset = dt_entry_v1->offset;
			cur_dt_entry->size = dt_entry_v1->size;
			table_ptr += sizeof(struct dt_entry_v1);
			break;
		case DEV_TREE_VERSION_V2:
			dt_entry_v2 = (struct dt_entry_v2*)table_ptr;
            memcpy(&new_dt_entry_v2, dt_entry_v2, sizeof(new_dt_entry_v2));
			cur_dt_entry->platform_id = dt_entry_v2->platform_id;
			cur_dt_entry->variant_id = dt_entry_v2->variant_id;
			cur_dt_entry->soc_rev = dt_entry_v2->soc_rev;
			/* For V2 version of DTBs we have platform version field as part
			 * of variant ID, in such case the subtype will be mentioned as 0x0
			 * As the qcom, board-id = <0xSSPMPmPH, 0x0>
			 * SS -- Subtype
			 * PM -- Platform major version
			 * Pm -- Platform minor version
			 * PH -- Platform hardware CDP/MTP
			 * In such case to make it compatible with LK algorithm move the subtype
			 * from variant_id to subtype field
			 */
			if (dt_entry_v2->board_hw_subtype == 0)
				cur_dt_entry->board_hw_subtype = (cur_dt_entry->variant_id >> 0x18);
			else
				cur_dt_entry->board_hw_subtype = dt_entry_v2->board_hw_subtype;
			/*cur_dt_entry->pmic_rev[0] = board_pmic_target(0);
			cur_dt_entry->pmic_rev[1] = board_pmic_target(1);
			cur_dt_entry->pmic_rev[2] = board_pmic_target(2);
			cur_dt_entry->pmic_rev[3] = board_pmic_target(3);*/
			cur_dt_entry->offset = dt_entry_v2->offset;
			cur_dt_entry->size = dt_entry_v2->size;
			table_ptr += sizeof(struct dt_entry_v2);
			break;
		case DEV_TREE_VERSION_V3:
			memcpy(cur_dt_entry, (struct dt_entry *)table_ptr,
				   sizeof(struct dt_entry));
            memcpy(&new_dt_entry_v3, table_ptr, sizeof(new_dt_entry_v3));
			/* For V3 version of DTBs we have platform version field as part
			 * of variant ID, in such case the subtype will be mentioned as 0x0
			 * As the qcom, board-id = <0xSSPMPmPH, 0x0>
			 * SS -- Subtype
			 * PM -- Platform major version
			 * Pm -- Platform minor version
			 * PH -- Platform hardware CDP/MTP
			 * In such case to make it compatible with LK algorithm move the subtype
			 * from variant_id to subtype field
			 */
			if (cur_dt_entry->board_hw_subtype == 0)
				cur_dt_entry->board_hw_subtype = (cur_dt_entry->variant_id >> 0x18);

			table_ptr += sizeof(struct dt_entry);
			break;
		default:
			fprintf(stderr, "ERROR: Unsupported version (%d) in DT table \n",
					table->version);
			return -1;
		}

        fprintf(stdout, "entry: <%u %u 0x%x>\n",
					cur_dt_entry->platform_id,
					cur_dt_entry->variant_id,
					cur_dt_entry->soc_rev);

        // create empty fdt
        rc = fdt_create_empty_tree(fdt, sizeof(fdt));
        if(rc) {
			fprintf(stderr, "Can't create empty FDT: %s\n", fdt_strerror(rc));
            return -1;
        }

	    // get root node
	    rc = fdt_path_offset(fdt, "/");
        if(rc) {
			fprintf(stderr, "Can't get root node: %s\n", fdt_strerror(rc));
            return -1;
        }
	    uint32_t offset = rc;

        uint32_t msm_id[3];
        uint32_t board_id[2];
        switch(table->version) {
		    case DEV_TREE_VERSION_V1:
                // set msm-id
                msm_id[0] = cpu_to_fdt32(cur_dt_entry->platform_id);
                msm_id[1] = cpu_to_fdt32(cur_dt_entry->variant_id);
                msm_id[2] = cpu_to_fdt32(cur_dt_entry->soc_rev);
                rc = fdt_setprop(fdt, offset, "qcom,msm-id", &msm_id, sizeof(*msm_id)*3);
                if(rc) {
			        fprintf(stderr, "Can't set qcom,msm-id: %s\n", fdt_strerror(rc));
                    return -1;
                }
                break;
		    case DEV_TREE_VERSION_V2:
                // set msm-id
                msm_id[0] = cpu_to_fdt32(cur_dt_entry->platform_id);
                msm_id[1] = cpu_to_fdt32(cur_dt_entry->soc_rev);
                rc = fdt_setprop(fdt, offset, "qcom,msm-id", &msm_id, sizeof(*msm_id)*2);
                if(rc) {
			        fprintf(stderr, "Can't set qcom,msm-id: %s\n", fdt_strerror(rc));
                    return -1;
                }

                // set board-id
                board_id[0] = cpu_to_fdt32(cur_dt_entry->variant_id);
                board_id[1] = cpu_to_fdt32(cur_dt_entry->board_hw_subtype);
                rc = fdt_setprop(fdt, offset, "qcom,board-id", &board_id, sizeof(*board_id)*2);
                if(rc) {
			        fprintf(stderr, "Can't set qcom,msm-id: %s\n", fdt_strerror(rc));
                    return -1;
                }
                break;

		    default:
			    fprintf(stderr, "Unsupported version (%d) in DT table \n",
					    table->version);
			    return -1;
        }

        // set model
        rc = fdt_setprop_string(fdt, offset, "model", "EFIDroid");
        if(rc) {
			fprintf(stderr, "Can't set model: %s\n", fdt_strerror(rc));
            return -1;
        }

        // set size cells
        rc = fdt_setprop_u32(fdt, offset, "#size-cells", 0x1);
        if(rc) {
			fprintf(stderr, "Can't set size_cells: %s\n", fdt_strerror(rc));
            return -1;
        }

        // set addr cells
        rc = fdt_setprop_u32(fdt, offset, "#address-cells", 0x1);
        if(rc) {
			fprintf(stderr, "Can't set addr_cells: %s\n", fdt_strerror(rc));
            return -1;
        }

        // add chosen subnode
        rc = fdt_add_subnode(fdt, offset, "chosen");
        if(rc<0) {
			fprintf(stderr, "Can't create chosen node: %s\n", fdt_strerror(rc));
            return -1;
        }

        // add memory subnode
        rc = fdt_add_subnode(fdt, offset, "memory");
        if(rc<0) {
			fprintf(stderr, "Can't create chosen node: %s\n", fdt_strerror(rc));
            return -1;
        }
        offset = rc;

        uint32_t reg[4];
        rc = fdt_setprop(fdt, offset, "reg", &reg, sizeof(*reg)*4);
        if(rc) {
	        fprintf(stderr, "Can't set qcom,msm-id: %s\n", fdt_strerror(rc));
            return -1;
        }

        // set device_type
        rc = fdt_setprop_string(fdt, offset, "device_type", "memory");
        if(rc) {
			fprintf(stderr, "Can't set device_type: %s\n", fdt_strerror(rc));
            return -1;
        }

        // set size cells
        rc = fdt_setprop_u32(fdt, offset, "#size-cells", 0x1);
        if(rc) {
			fprintf(stderr, "Can't set size_cells: %s\n", fdt_strerror(rc));
            return -1;
        }

        // set addr cells
        rc = fdt_setprop_u32(fdt, offset, "#address-cells", 0x1);
        if(rc) {
			fprintf(stderr, "Can't set addr_cells: %s\n", fdt_strerror(rc));
            return -1;
        }

        // pack fdt
        rc = fdt_pack(fdt);
        if(rc) {
			fprintf(stderr, "Can't pack FDT: %s\n", fdt_strerror(rc));
            return -1;
        }

        // get fdt size
        uint32_t fdt_size = fdt_totalsize(fdt);

        // get current fd position
        off_t cur_pos = fdpos(fd);
        if(cur_pos<0) {
			fprintf(stderr, "Can't get position of file: %s\n", filename);
            return (int)cur_pos;
        }

        // write dt entry
        switch(table->version) {
		case DEV_TREE_VERSION_V1:
            new_dt_entry_v1.offset = cur_pos + sizeof(new_dt_entry_v1);
            new_dt_entry_v1.size = fdt_size;
            write(fd, &new_dt_entry_v1, sizeof(new_dt_entry_v1));
			break;
		case DEV_TREE_VERSION_V2:
            new_dt_entry_v2.offset = cur_pos + sizeof(new_dt_entry_v2);
            new_dt_entry_v2.size = fdt_size;
            write(fd, &new_dt_entry_v2, sizeof(new_dt_entry_v2));
			break;
		case DEV_TREE_VERSION_V3:
            new_dt_entry_v3.offset = cur_pos + sizeof(new_dt_entry_v3);
            new_dt_entry_v3.size = fdt_size;
            write(fd, &new_dt_entry_v3, sizeof(new_dt_entry_v3));
			break;
		default:
			fprintf(stderr, "ERROR: Unsupported version (%d) in DT table \n",
					table->version);
			return -1;
		}

        // write fdt
    	write(fd, fdt, fdt_size);
	}    

    return 0;
}

int main(int argc, char** argv) {
    int rc;
    off_t off;
    void* dtimg = NULL;
    ssize_t ssize;

    // validate arguments
    if(argc!=3) {
        fprintf(stderr, "Usage: %s dt.img newdt.img\n", argv[0]);
        return -EINVAL;
    }

    // open file
    const char* filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if(fd<0) {
        fprintf(stderr, "Can't open file %s\n", filename);
        return fd;
    }

    // get filesize
    off = fdsize(fd);
    if(off<0) {
        fprintf(stderr, "Can't get size of file %s\n", filename);
        rc = (int)off;
        goto close_file;
    }

    // allocate buffer
    dtimg = malloc(off);
    if(!dtimg) {
        fprintf(stderr, "Can't allocate buffer of size %lu\n", off);
        rc = -ENOMEM;
        goto close_file;
    }

    // read file into memory
    ssize = read(fd, dtimg, off);
    if(ssize!=off) {
        fprintf(stderr, "Can't read file %s into buffer\n", filename);
        rc = (int)ssize;
        goto free_buffer;
    }

    // validate devicetree
    uint32_t dt_hdr_size;
    rc = dev_tree_validate(dtimg, m_page_size, &dt_hdr_size);
    if (rc) {
        fprintf(stderr, "Cannot validate Device Tree Table \n");
        goto free_buffer;
    }

    // generate devtree
    struct dt_table* table = dtimg;
    rc = dev_tree_generate(argv[2], table);
    if (rc) {
        fprintf(stderr, "Cannot process table\n");
        goto free_buffer;
    }

free_buffer:
    free(dtimg);

close_file:
    // close file
    if(close(fd)) {
        fprintf(stderr, "Can't close file %s\n", filename);
        return rc;
    }

    if(rc) {
        fprintf(stderr, "ERROR: %s\n", strerror(-rc));
        return rc;
    }

    return rc;
}
