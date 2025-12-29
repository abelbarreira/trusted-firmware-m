/*
 * SPDX-FileCopyrightText: Copyright The TrustedFirmware-M Contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "psa/error.h"
#include "gpt.h"
#include "gpt_flash.h"
#include "tfm_log.h"
#include "efi_guid_structs.h"

/* This needs to be defined by the platform and is used by the GPT library as
 * the number of bytes in a Logical Block Address (LBA)
 */
#ifndef TFM_GPT_BLOCK_SIZE
#error "TFM_GPT_BLOCK_SIZE must be defined if using GPT library!"
#endif

/* Where Master Boot Record (MBR) is on flash */
#define MBR_LBA 0ULL

/* Number of unused bytes at the start of the MBR */
#define MBR_UNUSED_BYTES 446

/* Cylinder Head Sector (CHS) length for MBR entry */
#define MBR_CHS_ADDRESS_LEN 3

/* Number of entries in an MBR */
#define MBR_NUM_ENTRIES 4

/* MBR signature as defined by UEFI spec */
#define MBR_SIG 0xAA55

/* Type of MBR partition that indicates GPT in use */
#define MBR_TYPE_GPT 0xEE

/* Default GUID Partition Table (GPT) header size */
#define GPT_HEADER_SIZE 92

/* "EFI PART" (without null byte) */
#define GPT_SIG "EFI PART"
#define GPT_SIG_LEN 8

/* Default partition entry size */
#define GPT_ENTRY_SIZE 128

/* Minimum number of partition entries according to spec */
#define GPT_MIN_PARTITIONS 4

/* Logical Block Address (LBA) for primary GPT */
#define PRIMARY_GPT_LBA 1

/* LBA for primary GPT partition array */
#define PRIMARY_GPT_ARRAY_LBA 2

/* MBR partition entry - both for protective MBR entry and
 * legacy MBR entry
 */
struct mbr_entry_t {
    /* Indicates if bootable */
    uint8_t status;
    /* For legacy MBR, not used by UEFI firmware. For protective MBR, set to
     * 0x000200
     */
    uint8_t first_sector[MBR_CHS_ADDRESS_LEN];
    /* Type of partition */
    uint8_t os_type;
    /* For legacy MBR, not used by UEFI firmware. For protective MBR, last
     * block on flash.
     */
    uint8_t last_sector[MBR_CHS_ADDRESS_LEN];
    /* For legacy MBR, starting LBA of partition. For protective MBR, set to
     * 0x00000001
     */
    uint32_t first_lba;
    /* For legacy MBR, size of partition. For protective MBR, size of flash
     * minus one
     */
    uint32_t size;
} __attribute__((packed));

/* MBR. This structure is used both for protective MBR and legacy MBR. The boot
 * code, flash signature and unknown sections are not read because they are
 * unused and do not change.
 */
struct mbr_t {
    /* Boot code at offset 0 is unused in EFI */
    /* Unique MBR Disk signature at offset 440 is unused */
    /* The next 2 bytes are also unused */
    /* Array of four MBR partition records. For protective MBR, only the first
     * is valid
     */
    struct mbr_entry_t partitions[MBR_NUM_ENTRIES];
    /* 0xAA55 */
    uint16_t sig;
} __attribute__((packed));

/* A GPT partition entry. */
 struct gpt_entry_t {
    struct efi_guid_t partition_type;   /* Partition type, defining purpose */
    struct efi_guid_t unique_guid;      /* Unique GUID that defines each partition */
    uint64_t start;                     /* Starting LBA for partition */
    uint64_t end;                       /* Ending LBA for partition */
    uint64_t attr;                      /* Attribute bits */
    char name[GPT_ENTRY_NAME_LENGTH];   /* Human readable name for partition */
}  __attribute__((packed));

/* The GPT header. */
struct gpt_header_t {
    char signature[GPT_SIG_LEN];    /* "EFI PART" */
    uint32_t revision;              /* Revision number */
    uint32_t size;                  /* Size of this header */
    uint32_t header_crc;            /* CRC of this header */
    uint32_t reserved;              /* Reserved */
    uint64_t current_lba;           /* LBA of this header */
    uint64_t backup_lba;            /* LBA of backup GPT header */
    uint64_t first_lba;             /* First usable LBA */
    uint64_t last_lba;              /* Last usable LBA */
    struct efi_guid_t flash_guid;   /* Disk GUID */
    uint64_t array_lba;             /* First LBA of array of partition entries */
    uint32_t num_partitions;        /* Number of partition entries in array */
    uint32_t entry_size;            /* Size of a single partition entry */
    uint32_t array_crc;             /* CRC of partitions array */
} __attribute__((packed));

/* A GUID partition table in memory. The array is not stored in memory
 * due to its size
 */
struct gpt_t {
    struct gpt_header_t header;     /* GPT header */
    uint32_t num_used_partitions;   /* Number of in-use partitions */
};

/* A function for comparing some gpt entry's attribute with something known.
 * Used to find entries of a certain kind, such as with a particular GUID,
 * name or type.
 */
typedef bool (*gpt_entry_cmp_t)(const struct gpt_entry_t *, const void *);

/* The LBA for the backup table */
static uint64_t backup_gpt_lba = 0;

/* The flash driver, used to perform I/O */
static struct gpt_flash_driver_t *plat_flash_driver = NULL;

/* Maximum partitions on platform */
static uint32_t plat_max_partitions = 0;

/* The primary GPT (also used if legacy MBR, but GPT header and partition
 * entries are zero'd)
 */
static struct gpt_t primary_gpt = {0};

/* Buffer to use for LBA I/O */
static uint8_t lba_buf[TFM_GPT_BLOCK_SIZE] = {0};

/* LBA that is cached in the buffer. Zero is valid only for protective MBR, all
 * other GPT operations must have LBA of one or greater
 */
static uint64_t cached_lba = 0;

/* Helper function prototypes */
__attribute__((unused))
static void print_guid(struct efi_guid_t guid);
__attribute__((unused))
static void dump_table(const struct gpt_t *table, bool header_only);
__attribute__((unused))
static psa_status_t unicode_to_ascii(const char *unicode, char *ascii);
static inline uint64_t partition_entry_lba(const struct gpt_t *table,
                                           uint32_t array_index);
static inline uint64_t gpt_entry_per_lba_count(void);
static psa_status_t count_used_partitions(const struct gpt_t *table,
                                          uint32_t *num_used);
static psa_status_t read_from_flash(uint64_t required_lba);
static psa_status_t read_entry_from_flash(const struct gpt_t *table,
                                          uint32_t array_index,
                                          struct gpt_entry_t *entry);
static psa_status_t read_table_from_flash(struct gpt_t *table, bool is_primary);
static psa_status_t find_gpt_entry(const struct gpt_t      *table,
                                   gpt_entry_cmp_t          compare,
                                   const void              *attr,
                                   const uint32_t           repeat_index,
                                   struct gpt_entry_t      *entry,
                                   uint32_t                *array_index);
static psa_status_t mbr_load(struct mbr_t *mbr);
static bool gpt_entry_cmp_guid(const struct gpt_entry_t *entry, const void *guid);

/* PUBLIC API FUNCTIONS */

psa_status_t gpt_entry_read(const struct efi_guid_t  *guid,
                            struct partition_entry_t *partition_entry)
{
    struct gpt_entry_t cached_entry;
    const psa_status_t ret = find_gpt_entry(&primary_gpt, gpt_entry_cmp_guid, guid, 0, &cached_entry, NULL);
    if (ret != PSA_SUCCESS) {
        return ret;
    }

    partition_entry->start = cached_entry.start;
    partition_entry->size = cached_entry.end - cached_entry.start + 1;
    memcpy(partition_entry->name, cached_entry.name, GPT_ENTRY_NAME_LENGTH);
    partition_entry->attr = cached_entry.attr;
    partition_entry->partition_guid = cached_entry.unique_guid;
    partition_entry->type_guid = cached_entry.partition_type;

    return PSA_SUCCESS;
}

/* Initialises GPT from first block. */
psa_status_t gpt_init(struct gpt_flash_driver_t *flash_driver, uint64_t max_partitions)
{
    cached_lba = 0;
    if (max_partitions < GPT_MIN_PARTITIONS) {
        ERROR("Minimum number of partitions is %d\n", GPT_MIN_PARTITIONS);
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    if (flash_driver->read == NULL) {
        ERROR("I/O functions must be defined\n");
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    /* Retain information needed to perform I/O. */
    if (plat_flash_driver == NULL) {
        plat_flash_driver = flash_driver;
    }
    if (plat_max_partitions == 0) {
        plat_max_partitions = max_partitions;
    }
    if (plat_flash_driver->init != NULL) {
        if (plat_flash_driver->init() != 0) {
            ERROR("Unable to initialise flash driver\n");
            return PSA_ERROR_STORAGE_FAILURE;
        }
    }

    struct mbr_t mbr;
    psa_status_t ret = mbr_load(&mbr);
    if (ret != PSA_SUCCESS) {
        goto fail_load;
    }

    /* If the first record has type 0xEE (GPT protective), then the flash uses
     * GPT. Else, treat it as legacy MBR
     */
    if (mbr.partitions[0].os_type == MBR_TYPE_GPT) {
        ret = read_table_from_flash(&primary_gpt, true);
    } else {
        WARN("Unsupported legacy MBR in use\n");
        ret = PSA_ERROR_NOT_SUPPORTED;
    }

    if (ret != PSA_SUCCESS) {
        goto fail_load;
    }

    /* Count the number of used entries, assuming the array is not sparese */
    ret = count_used_partitions(&primary_gpt, &primary_gpt.num_used_partitions);
    if (ret != PSA_SUCCESS) {
        goto fail_load;
    }

    /* Read the backup GPT and cache necessary values */
    backup_gpt_lba = primary_gpt.header.backup_lba;
    if (backup_gpt_lba != 0) {
        struct gpt_t backup_gpt;
        ret = read_table_from_flash(&backup_gpt, false);
        if (ret != PSA_SUCCESS) {
            goto fail_load;
        }
    } else {
        WARN("Backup GPT location is unknown!\n");
    }

    return PSA_SUCCESS;

fail_load:
    /* Reset so that the user can try with something else if desired */
    plat_flash_driver = NULL;
    plat_max_partitions = 0;
    backup_gpt_lba = 0;
    cached_lba = 0;

    return ret;
}

psa_status_t gpt_uninit(void)
{
    psa_status_t ret = PSA_SUCCESS;

    if (plat_flash_driver) {
        /* Uninitialise driver if function provided */
        if (plat_flash_driver->uninit != NULL) {
            if (plat_flash_driver->uninit() != 0) {
                ERROR("Unable to uninitialise flash driver\n");
                ret = PSA_ERROR_STORAGE_FAILURE;
            }
        }
    }

    plat_flash_driver = NULL;
    plat_max_partitions = 0;
    backup_gpt_lba = 0;
    cached_lba = 0;

    return ret;
}

/* Returns the number of partition entries in each LBA */
static inline uint64_t gpt_entry_per_lba_count(void)
{
    static uint64_t num_entries = 0;
    if (num_entries == 0) {
        num_entries = TFM_GPT_BLOCK_SIZE / primary_gpt.header.entry_size;
    }
    return num_entries;
}

/* Compare the entry with the given guid */
static bool gpt_entry_cmp_guid(const struct gpt_entry_t *entry, const void *guid)
{
    const struct efi_guid_t *cmp_guid = (const struct efi_guid_t *)guid;
    const struct efi_guid_t entry_guid = entry->unique_guid;

    return efi_guid_cmp(&entry_guid, cmp_guid) == 0;
}

/* Read entry with given GUID from given table and return it if found. */
static psa_status_t find_gpt_entry(const struct gpt_t        *table,
                                   gpt_entry_cmp_t            compare,
                                   const void                *cmp_attr,
                                   const uint32_t             repeat_index,
                                   struct gpt_entry_t        *entry,
                                   uint32_t                  *array_index)
{
    if (table->num_used_partitions == 0) {
        return PSA_ERROR_DOES_NOT_EXIST;
    }

    uint32_t num_found = 0;
    bool io_failure = false;
    for (uint32_t i = 0; i < table->num_used_partitions; ++i) {
        const psa_status_t ret = read_entry_from_flash(table, i, entry);
        if (ret != PSA_SUCCESS) {
            /* This might not have been the partition being sought after anyway,
             * so may as well try the rest
             */
            io_failure = true;
            continue;
        }

        if (compare(entry, cmp_attr) && num_found++ == repeat_index) {
            if (array_index != NULL) {
                *array_index = i;
            }
            return PSA_SUCCESS;
        }
    }

    return io_failure ? PSA_ERROR_STORAGE_FAILURE : PSA_ERROR_DOES_NOT_EXIST;
}

/* Load MBR from flash */
static psa_status_t mbr_load(struct mbr_t *mbr)
{
    /* Read the beginning of the first block of flash, which will contain either
     * a legacy MBR or a protective MBR (in the case of GPT). The first
     * MBR_UNUSED_BYTES are unused and so do not need to be considered.
     */
    ssize_t ret = plat_flash_driver->read(MBR_LBA, lba_buf);
    if (ret != TFM_GPT_BLOCK_SIZE) {
        ERROR("Unable to read from flash at block 0x%08x%08x\n",
                (uint32_t)(MBR_LBA >> 32),
                (uint32_t)MBR_LBA);
        return PSA_ERROR_STORAGE_FAILURE;
    }
    memcpy(mbr, lba_buf + MBR_UNUSED_BYTES, sizeof(*mbr));

    /* Check MBR boot signature */
    if (mbr->sig != MBR_SIG) {
        ERROR("MBR signature incorrect\n");
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    return PSA_SUCCESS;
}

/* Reads an LBA from the flash device and caches it. If the requested LBA is
 * already cached, this is a no-op
 */
static psa_status_t read_from_flash(uint64_t required_lba)
{
    ssize_t ret;

    if (required_lba != cached_lba) {
        ret = plat_flash_driver->read(required_lba, lba_buf);
        if (ret != TFM_GPT_BLOCK_SIZE) {
            ERROR("Unable to read from flash at block 0x%08x%08x\n",
                    (uint32_t)(required_lba >> 32),
                    (uint32_t)required_lba);
            return PSA_ERROR_STORAGE_FAILURE;
        }
        cached_lba = required_lba;
    }

    return PSA_SUCCESS;
}

/* Returns the LBA that a particular partition entry is in based on its position
 * in the array
 */
static uint64_t inline partition_entry_lba(const struct gpt_t *table,
                                           uint32_t array_index)
{
    return table->header.array_lba + (array_index / gpt_entry_per_lba_count());
}

/* Returns the number of partition entries used in the array, assuming the
 * array is not sparse
 */
static psa_status_t count_used_partitions(const struct gpt_t *table,
                                          uint32_t *num_used)
{
    for (uint32_t i = 0; i < table->header.num_partitions; ++i) {
        struct gpt_entry_t entry = {0};
        const psa_status_t ret = read_entry_from_flash(table, i, &entry);
        if (ret != PSA_SUCCESS) {
            return ret;
        }

        const struct efi_guid_t null_guid = NULL_GUID;
        const struct efi_guid_t entry_guid = entry.partition_type;
        if (efi_guid_cmp(&null_guid, &entry_guid) == 0) {
            *num_used = i;
            return PSA_SUCCESS;
        }
    }

    *num_used = table->header.num_partitions;
    return PSA_SUCCESS;
}

/* Reads a GPT entry from the given table on flash */
static psa_status_t read_entry_from_flash(const struct gpt_t *table,
                                          uint32_t            array_index,
                                          struct gpt_entry_t *entry)
{
    uint64_t required_lba = partition_entry_lba(table, array_index);
    const psa_status_t ret = read_from_flash(required_lba);
    if (ret != PSA_SUCCESS) {
        return ret;
    }

    memcpy(
            entry,
            lba_buf + ((array_index % gpt_entry_per_lba_count()) * table->header.entry_size),
            GPT_ENTRY_SIZE);

    return PSA_SUCCESS;
}

/* Reads a GPT table from flash */
static psa_status_t read_table_from_flash(struct gpt_t *table, bool is_primary)
{
    if (!is_primary && backup_gpt_lba == 0) {
        ERROR("Backup GPT location unknown!\n");
        return PSA_ERROR_STORAGE_FAILURE;
    }

    const psa_status_t ret = read_from_flash(is_primary ? PRIMARY_GPT_LBA : backup_gpt_lba);
    if (ret != PSA_SUCCESS) {
        return ret;
    }

    memcpy(&(table->header), lba_buf, GPT_HEADER_SIZE);

    return PSA_SUCCESS;
}

/* Converts unicode string to valid ascii */
static psa_status_t unicode_to_ascii(const char *unicode, char *ascii)
{
    /* Check whether the unicode string is valid */
    if (unicode[0] == '\0') {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 1; i < GPT_ENTRY_NAME_LENGTH; i += 2) {
        if (unicode[i] != '\0') {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
    }

    /* Convert the unicode string to ascii string */
    for (int i = 0; i < GPT_ENTRY_NAME_LENGTH; i += 2) {
        ascii[i >> 1] = unicode[i];
        if (unicode[i] == '\0') {
            break;
        }
    }

    return PSA_SUCCESS;
}

/* Prints guid in human readable format. Useful for debugging but should never
 * be used in production, hence marked as unused
 */
static void print_guid(struct efi_guid_t guid)
{
    INFO("%04x%04x-%04x-%04x-%04x-%04x%04x%04x\n",
            ((uint16_t *)&guid)[0],
            ((uint16_t *)&guid)[1],
            ((uint16_t *)&guid)[2],
            ((uint16_t *)&guid)[3],
            ((uint16_t *)&guid)[4],
            ((uint16_t *)&guid)[5],
            ((uint16_t *)&guid)[6],
            ((uint16_t *)&guid)[7]);
}

/* Dumps header and optionally meta-data about array. Useful for debugging,
 * but should never be used in production, hence marked as unused.
 */
static void dump_table(const struct gpt_t *table, bool header_only)
{
    /* Print the header first */
    const struct gpt_header_t *header = &(table->header);
    INFO("----------\n");
    INFO("Signature: %8s\n", header->signature);
    INFO("Revision: 0x%08x\n", header->revision);
    INFO("HeaderSize: 0x%08x\n", header->size);
    INFO("HeaderCRC32: 0x%08x\n", header->header_crc);
    INFO("Reserved: 0x%08x\n", header->reserved);
    INFO("MyLBA: 0x%08x%08x\n",
            (uint32_t)(header->current_lba >> 32),
            (uint32_t)(header->current_lba));
    INFO("AlternateLBA: 0x%08x%08x\n",
            (uint32_t)(header->backup_lba >> 32),
            (uint32_t)(header->backup_lba));
    INFO("FirstUsableLBA: 0x%08x%08x\n",
            (uint32_t)(header->first_lba >> 32),
            (uint32_t)(header->first_lba));
    INFO("LastUsableLBA: 0x%08x%08x\n",
            (uint32_t)(header->last_lba >> 32),
            (uint32_t)(header->last_lba));
    INFO("DiskGUID: ");
    print_guid(header->flash_guid);
    INFO("ParitionEntryLBA: 0x%08x%08x\n",
            (uint32_t)(header->array_lba >> 32),
            (uint32_t)(header->array_lba));
    INFO("NumberOfPartitionEntries: 0x%08x\n", header->num_partitions);
    INFO("SizeOfPartitionEntry: 0x%08x\n", header->entry_size);
    INFO("PartitionEntryArrayCRC32: 0x%08x\n", header->array_crc);
    INFO("----------\n");

    if (!header_only) {
        /* Now print meta-data for each entry, including those not in use */
        for (uint32_t i = 0; i < table->num_used_partitions; ++i) {
            struct gpt_entry_t entry;
            psa_status_t ret = read_entry_from_flash(&primary_gpt, i, &entry);
            if (ret != PSA_SUCCESS) {
                continue;
            }

            INFO("Entry number: %u\n", i);
            INFO("\tPartitionTypeGUID: ");
            print_guid(entry.partition_type);
            INFO("\tUniquePartitionGUID: ");
            print_guid(entry.unique_guid);
            INFO("\tStartingLBA: 0x%08x%08x\n",
                    (uint32_t)(entry.start >> 32),
                    (uint32_t)(entry.start));
            INFO("\tEndingLBA: 0x%08x%08x\n",
                    (uint32_t)(entry.end >> 32),
                    (uint32_t)(entry.end));
            INFO("\tAttributes: 0x%08x%08x\n",
                    (uint32_t)(entry.attr >> 32),
                    (uint32_t)(entry.attr));
            char name[GPT_ENTRY_NAME_LENGTH >> 1];
            if (unicode_to_ascii(entry.name, name) != PSA_SUCCESS) {
                INFO("\tPartitionName: [Not valid ascii]\n");
            } else {
                INFO("\tPartitionName: %s\n", name);
            }
        }
    }
}
