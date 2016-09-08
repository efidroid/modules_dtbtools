#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <smem.h>

#define STRCASE(a) case a: return #a;
#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

struct smem_proc_comm {
    unsigned command;
    unsigned status;
    unsigned data1;
    unsigned data2;
};
typedef struct smem_proc_comm smem_proc_comm_t;

struct smem_heap_info {
    unsigned initialized;
    unsigned free_offset;
    unsigned heap_remaining;
    unsigned reserved;
};
typedef struct smem_heap_info smem_heap_info_t;

struct smem_alloc_info {
    unsigned allocated;
    unsigned offset;
    unsigned size;
    unsigned base_ext;
};
typedef struct smem_alloc_info smem_alloc_info_t;

struct smem {
    struct smem_proc_comm proc_comm[4];
    unsigned version_info[32];
    struct smem_heap_info heap_info;
    struct smem_alloc_info alloc_info[0];
};
typedef struct smem smem_t;

static const char *cmd = "";

static const char *smemtype2str(int type)
{
    if (type>SMEM_SMD_BASE_ID && type<SMEM_SMEM_LOG_IDX) {
        return "within-SMD_BASE_ID";
    }
    if (type>SMEM_VERSION_FIRST && type<SMEM_VERSION_LAST) {
        return "within-SMEM_VERSION";
    }
    if (type>SMEM_SMD_BASE_ID_2 && type<SMEM_SMD_FIFO_BASE_ID_2) {
        return "within-SMD_BASE_ID_2";
    }
    if (type>SMEM_SMD_FIFO_BASE_ID_2 && type<SMEM_CHANNEL_ALLOC_TBL_2) {
        return "within-SMD_FIFO_BASE_ID_2";
    }
    if (type>SMEM_CHANNEL_ALLOC_TBL_2 && type<SMEM_I2C_MUTEX) {
        return "within-CHANNEL_ALLOC_TBL_2";
    }
    if (type>SMEM_SMD_FIFO_BASE_ID && type<SMEM_USABLE_RAM_PARTITION_TABLE) {
        return "within-SMD_FIFO_BASE_ID";
    }
    if (type>SMEM_USABLE_RAM_PARTITION_TABLE && type<SMEM_POWER_ON_STATUS_INFO) {
        return "within-USABLE_RAM_PARTITION_TABLE";
    }
    if (type>SMEM_SMP2P_APPS_BASE && type<SMEM_SMP2P_MODEM_BASE) {
        return "within-SMP2P_APPS";
    }
    if (type>SMEM_SMP2P_MODEM_BASE && type<SMEM_SMP2P_AUDIO_BASE) {
        return "within-SMP2P_MODEM";
    }
    if (type>SMEM_SMP2P_AUDIO_BASE && type<SMEM_SMP2P_WIRLESS_BASE) {
        return "within-SMP2P_AUDIO";
    }
    if (type>SMEM_SMP2P_WIRLESS_BASE && type<SMEM_SMP2P_POWER_BASE) {
        return "within-SMP2P_WIRLESS";
    }
    if (type>SMEM_SMP2P_POWER_BASE && type<SMEM_FLASH_DEVICE_INFO) {
        return "within-SMP2P_POWER";
    }
    if (type>SMEM_SMP2P_SENSOR_BASE && type<SMEM_SMP2P_TZ_BASE) {
        return "within-SMP2P_SENSOR";
    }
    if (type>SMEM_SMP2P_TZ_BASE && type<SMEM_IPA_FILTER_TABLE) {
        return "within-SMP2P_TZ";
    }

    switch (type) {
            STRCASE(SMEM_PROC_COMM);
            STRCASE(SMEM_HEAP_INFO);
            STRCASE(SMEM_ALLOCATION_TABLE);
            STRCASE(SMEM_VERSION_INFO);
            STRCASE(SMEM_HW_RESET_DETECT);
            STRCASE(SMEM_AARM_WARM_BOOT);
            STRCASE(SMEM_DIAG_ERR_MESSAGE);
            STRCASE(SMEM_SPINLOCK_ARRAY);
            STRCASE(SMEM_MEMORY_BARRIER_LOCATION);
            //STRCASE(SMEM_FIXED_ITEM_LAST);
            STRCASE(SMEM_AARM_PARTITION_TABLE);
            STRCASE(SMEM_AARM_BAD_BLOCK_TABLE);
            STRCASE(SMEM_ERR_CRASH_LOG_ADSP);
            STRCASE(SMEM_WM_UUID);
            STRCASE(SMEM_CHANNEL_ALLOC_TBL);
            STRCASE(SMEM_SMD_BASE_ID);
            STRCASE(SMEM_SMEM_LOG_IDX);
            STRCASE(SMEM_SMEM_LOG_EVENTS);
            STRCASE(SMEM_SMEM_STATIC_LOG_IDX);
            STRCASE(SMEM_SMEM_STATIC_LOG_EVENTS);
            STRCASE(SMEM_SMEM_SLOW_CLOCK_SYNC);
            STRCASE(SMEM_SMEM_SLOW_CLOCK_VALUE);
            STRCASE(SMEM_BIO_LED_BUF);
            STRCASE(SMEM_SMSM_SHARED_STATE);
            STRCASE(SMEM_SMSM_INT_INFO);
            STRCASE(SMEM_SMSM_SLEEP_DELAY);
            STRCASE(SMEM_SMSM_LIMIT_SLEEP);
            STRCASE(SMEM_SLEEP_POWER_COLLAPSE_DISABLED);
            STRCASE(SMEM_KEYPAD_KEYS_PRESSED);
            STRCASE(SMEM_KEYPAD_STATE_UPDATED);
            STRCASE(SMEM_KEYPAD_STATE_IDX);
            STRCASE(SMEM_GPIO_INT);
            STRCASE(SMEM_MDDI_LCD_IDX);
            STRCASE(SMEM_MDDI_HOST_DRIVER_STATE);
            STRCASE(SMEM_MDDI_LCD_DISP_STATE);
            STRCASE(SMEM_LCD_CUR_PANEL);
            STRCASE(SMEM_MARM_BOOT_SEGMENT_INFO);
            STRCASE(SMEM_AARM_BOOT_SEGMENT_INFO);
            STRCASE(SMEM_SLEEP_STATIC);
            STRCASE(SMEM_SCORPION_FREQUENCY);
            STRCASE(SMEM_SMD_PROFILES);
            STRCASE(SMEM_TSSC_BUSY);
            STRCASE(SMEM_HS_SUSPEND_FILTER_INFO);
            STRCASE(SMEM_BATT_INFO);
            STRCASE(SMEM_APPS_BOOT_MODE);
            //STRCASE(SMEM_VERSION_FIRST);
            STRCASE(SMEM_VERSION_SMD);
            STRCASE(SMEM_VERSION_SMD_BRIDGE);
            STRCASE(SMEM_VERSION_SMSM);
            STRCASE(SMEM_VERSION_SMD_NWAY_LOOP);
            STRCASE(SMEM_VERSION_LAST);
            STRCASE(SMEM_OSS_RRCASN1_BUF1);
            STRCASE(SMEM_OSS_RRCASN1_BUF2);
            STRCASE(SMEM_ID_VENDOR0);
            STRCASE(SMEM_ID_VENDOR1);
            STRCASE(SMEM_ID_VENDOR2);
            STRCASE(SMEM_HW_SW_BUILD_ID);
            STRCASE(SMEM_SMD_BASE_ID_2);
            STRCASE(SMEM_SMD_FIFO_BASE_ID_2);
            STRCASE(SMEM_CHANNEL_ALLOC_TBL_2);
            STRCASE(SMEM_I2C_MUTEX);
            STRCASE(SMEM_SCLK_CONVERSION);
            STRCASE(SMEM_SMD_SMSM_INTR_MUX);
            STRCASE(SMEM_SMSM_CPU_INTR_MASK);
            STRCASE(SMEM_APPS_DEM_SLAVE_DATA);
            STRCASE(SMEM_QDSP6_DEM_SLAVE_DATA);
            STRCASE(SMEM_VSENSE_DATA);
            STRCASE(SMEM_CLKREGIM_SOURCES);
            STRCASE(SMEM_SMD_FIFO_BASE_ID);
            STRCASE(SMEM_USABLE_RAM_PARTITION_TABLE);
            STRCASE(SMEM_POWER_ON_STATUS_INFO);
            STRCASE(SMEM_DAL_AREA);
            STRCASE(SMEM_SMEM_LOG_POWER_IDX);
            STRCASE(SMEM_SMEM_LOG_POWER_WRAP);
            STRCASE(SMEM_SMEM_LOG_POWER_EVENTS);
            STRCASE(SMEM_ERR_CRASH_LOG);
            STRCASE(SMEM_ERR_F3_TRACE_LOG);
            STRCASE(SMEM_SMD_BRIDGE_ALLOC_TABLE);
            STRCASE(SMEM_SMDLITE_TABLE);
            STRCASE(SMEM_SD_IMG_UPGRADE_STATUS);
            STRCASE(SMEM_SEFS_INFO);
            STRCASE(SMEM_RESET_LOG);
            STRCASE(SMEM_RESET_LOG_SYMBOLS);
            STRCASE(SMEM_MODEM_SW_BUILD_ID);
            STRCASE(SMEM_SMEM_LOG_MPROC_WRAP);
            STRCASE(SMEM_BOOT_INFO_FOR_APPS);
            STRCASE(SMEM_SMSM_SIZE_INFO);
            STRCASE(SMEM_SMD_LOOPBACK_REGISTER);
            STRCASE(SMEM_SSR_REASON_MSS0);
            STRCASE(SMEM_SSR_REASON_WCNSS0);
            STRCASE(SMEM_SSR_REASON_LPASS0);
            STRCASE(SMEM_SSR_REASON_DSPS0);
            STRCASE(SMEM_SSR_REASON_VCODEC0);
            STRCASE(SMEM_VOICE);
            STRCASE(SMEM_SMP2P_APPS_BASE);
            STRCASE(SMEM_SMP2P_MODEM_BASE);
            STRCASE(SMEM_SMP2P_AUDIO_BASE);
            STRCASE(SMEM_SMP2P_WIRLESS_BASE);
            STRCASE(SMEM_SMP2P_POWER_BASE);
            STRCASE(SMEM_FLASH_DEVICE_INFO);
            STRCASE(SMEM_BAM_PIPE_MEMORY);
            STRCASE(SMEM_IMAGE_VERSION_TABLE);
            STRCASE(SMEM_LC_DEBUGGER);
            STRCASE(SMEM_FLASH_NAND_DEV_INFO);
            STRCASE(SMEM_A2_BAM_DESCRIPTOR_FIFO);
            STRCASE(SMEM_CPR_CONFIG);
            STRCASE(SMEM_CLOCK_INFO);
            STRCASE(SMEM_IPC_FIFO);
            STRCASE(SMEM_RF_EEPROM_DATA);
            STRCASE(SMEM_COEX_MDM_WCN);
            STRCASE(SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR);
            STRCASE(SMEM_GLINK_NATIVE_XPRT_FIFO_0);
            STRCASE(SMEM_GLINK_NATIVE_XPRT_FIFO_1);
            STRCASE(SMEM_SMP2P_SENSOR_BASE);
            STRCASE(SMEM_SMP2P_TZ_BASE);
            STRCASE(SMEM_IPA_FILTER_TABLE);
        default:
            return "unknown";
    }
}

off_t fdsize(int fd)
{
    off_t off;

    off = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0L, SEEK_SET);

    return off;
}

void hexdump(const void *ptr, size_t len)
{
    uintptr_t address = (uintptr_t)ptr;
    size_t count;

    for (count = 0 ; count < len; count += 16) {
        union {
            uint32_t buf[4];
            uint8_t  cbuf[16];
        } u;
        size_t s = ROUNDUP(MIN(len - count, 16), 4);
        size_t i;

        printf("0x%08lx: ", address);
        for (i = 0; i < s / 4; i++) {
            if (i==2) printf(" ");

            u.buf[i] = ((const uint32_t *)address)[i];
            printf("%02x ", (u.buf[i]&0x000000ff));
            printf("%02x ", (u.buf[i]&0x0000ff00)>>8);
            printf("%02x ", (u.buf[i]&0x00ff0000)>>16);
            printf("%02x ", (u.buf[i]&0xff000000)>>24);
        }
        for (; i < 4; i++) {
            printf("         ");
        }
        printf("|");

        for (i=0; i < 16; i++) {
            char c = u.cbuf[i];
            if (i < s && isprint(c)) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("|\n");
        address += 16;
    }
}

static int process_smem(smem_t *smem, uint32_t bufsz)
{
    uint32_t alloc_info_maxsz = bufsz - (sizeof(smem_t));
    if (alloc_info_maxsz%sizeof(smem_alloc_info_t) !=0) {
        fprintf(stderr, "WARNING: smem file size if not aligned to %lu\n", sizeof(smem_alloc_info_t));
    }

    uint32_t alloc_info_max_entries = alloc_info_maxsz / sizeof(smem_alloc_info_t);
    //printf("max number of smem entries: %u\n", alloc_info_max_entries);

    uint32_t i;
    for (i=0; i<=alloc_info_max_entries; i++) {
        smem_alloc_info_t *alloc_info = &smem->alloc_info[i];

        if (alloc_info->allocated==0) {
            if (alloc_info->offset || alloc_info->size || alloc_info->base_ext) {
                if (alloc_info->allocated!=1) {
                    //fprintf(stderr, "WARNING: entry %u looks invalid, stop.\n", i);
                    break;
                }

                fprintf(stderr, "WARNING: %u is not allocated but contains data\n", i);
            }
            continue;
        } else if (alloc_info->allocated!=1) {
            fprintf(stderr, "WARNING: %u has invalid value for 'allocated': %u\n", i, alloc_info->allocated);
        }

        printf("[%u=%s] offset=%u size=%u base_ext=0x%08x\n", i, smemtype2str(i), alloc_info->offset, alloc_info->size, alloc_info->base_ext);
        if (alloc_info->base_ext) {
            //fprintf(stderr, "WARNING: %u has a base_ext. not dumping data.\n", i);
            continue;
        }

        void *dataptr = ((void *)smem) + alloc_info->offset;
        if (!strcmp(cmd, "hexdump"))
            hexdump(dataptr, alloc_info->size);
    }

    return 0;
}

int main(int argc, char **argv)
{
    int rc;
    off_t off;
    ssize_t ssize;
    void *smem = NULL;

    // validate arguments
    if (argc<2) {
        fprintf(stderr, "Usage: %s smem.bin\n", argv[0]);
        return -EINVAL;
    }
    const char *filename = argv[1];
    if (argc>=3) cmd = argv[2];

    // open file
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
    smem = malloc(off);
    if (!smem) {
        fprintf(stderr, "Can't allocate buffer of size %lu\n", off);
        rc = -ENOMEM;
        goto close_file;
    }

    // read file into memory
    ssize = read(fd, smem, off);
    if (ssize!=off) {
        fprintf(stderr, "Can't read file %s into buffer\n", filename);
        rc = (int)ssize;
        goto free_buffer;
    }

    // process smem
    process_smem(smem, off);

free_buffer:
    free(smem);

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


    return 0;
}
