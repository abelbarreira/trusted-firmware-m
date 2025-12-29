/*
 * SPDX-FileCopyrightText: Copyright The TrustedFirmware-M Contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <string.h>
#include <inttypes.h>

#include "unity.h"

#include "mock_tfm_log.h"
#include "mock_tfm_vprintf.h"

#include "efi_guid_structs.h"
#include "gpt_flash.h"
#include "gpt.h"
#include "psa/error.h"

/* Basic mocked disk layout and number of partitions */
#define TEST_BLOCK_SIZE 512
#define TEST_DISK_NUM_BLOCKS 128
#define TEST_MAX_PARTITIONS 4
#define TEST_DEFAULT_NUM_PARTITIONS TEST_MAX_PARTITIONS - 1

/* Maximum number of mocked reads per test */
#define TEST_MOCK_BUFFER_SIZE 512

/* Master Boot Record (MBR) definitions for test */
#define TEST_MBR_SIG 0xAA55
#define TEST_MBR_TYPE_GPT 0xEE
#define TEST_MBR_CHS_ADDRESS_LEN 3
#define TEST_MBR_NUM_PARTITIONS 4
#define TEST_MBR_UNUSED_BYTES 446

/* GUID Partition Table (GPT) header values */
#define TEST_GPT_SIG_LEN 8
#define TEST_GPT_SIG_INITIALISER {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'}
#define TEST_GPT_REVISION 0x00010000
#define TEST_GPT_HEADER_SIZE 92
#define TEST_GPT_CRC32 42
#define TEST_GPT_PRIMARY_LBA 1
#define TEST_GPT_BACKUP_LBA (TEST_DISK_NUM_BLOCKS - 1)
#define TEST_GPT_ARRAY_LBA (TEST_GPT_PRIMARY_LBA + 1)
#define TEST_GPT_BACKUP_ARRAY_LBA (TEST_GPT_BACKUP_LBA - 1)
#define TEST_GPT_FIRST_USABLE_LBA (TEST_GPT_ARRAY_LBA + 2)
#define TEST_GPT_LAST_USABLE_LBA (TEST_GPT_BACKUP_LBA - 2)
#define TEST_GPT_DISK_GUID MAKE_EFI_GUID(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)
#define TEST_GPT_ENTRY_SIZE 128

/* Test data. These are defined to be not fragmented */
#define TEST_GPT_FIRST_PARTITION_START TEST_GPT_FIRST_USABLE_LBA
#define TEST_GPT_FIRST_PARTITION_END (TEST_GPT_FIRST_PARTITION_START + 3)
#define TEST_GPT_SECOND_PARTITION_START (TEST_GPT_FIRST_PARTITION_END + 1)
#define TEST_GPT_SECOND_PARTITION_END (TEST_GPT_SECOND_PARTITION_START + 50)
#define TEST_GPT_THIRD_PARTITION_START (TEST_GPT_SECOND_PARTITION_END + 1)
#define TEST_GPT_THIRD_PARTITION_END (TEST_GPT_THIRD_PARTITION_START + 1)

/* Populates a backup header from a primary header and calculates the new CRC32 */
#define MAKE_BACKUP_HEADER(backup, primary)              \
    do {                                                 \
        memcpy(&backup, &primary, TEST_GPT_HEADER_SIZE); \
        backup.header_crc = TEST_GPT_CRC32;              \
        backup.current_lba = primary.backup_lba;         \
        backup.backup_lba = primary.current_lba;         \
        backup.array_lba = TEST_GPT_BACKUP_ARRAY_LBA;    \
    } while (0)

/* MBR partition entry */
struct mbr_entry_t {
    /* Indicates if bootable */
    uint8_t status;
    /* For legacy MBR, not used by UEFI firmware. For protective MBR, set to
     * 0x000200
     */
    uint8_t first_sector[TEST_MBR_CHS_ADDRESS_LEN];
    /* Type of partition */
    uint8_t os_type;
    /* For legacy MBR, not used by UEFI firmware. For protective MBR, last
     * block on disk.
     */
    uint8_t last_sector[TEST_MBR_CHS_ADDRESS_LEN];
    /* For legacy MBR, starting LBA of partition. For protective MBR, set to
     * 0x00000001
     */
    uint32_t first_lba;
    /* For legacy MBR, size of partition. For protective MBR, size of disk
     * minus one
     */
    uint32_t size;
} __attribute__((packed));

/* Master Boot Record. */
struct mbr_t {
    /* Unused bytes */
    uint8_t unused[TEST_MBR_UNUSED_BYTES];
    /* Array of four MBR partition records. For protective MBR, only the first
     * is valid
     */
    struct mbr_entry_t partitions[TEST_MBR_NUM_PARTITIONS];
    /* 0xAA55 */
    uint16_t sig;
} __attribute__((packed));

/* A gpt partition entry */
struct gpt_entry_t {
    struct efi_guid_t type;             /* Partition type */
    struct efi_guid_t guid;             /* Unique GUID */
    uint64_t start;                     /* Starting LBA for partition */
    uint64_t end;                       /* Ending LBA for partition */
    uint64_t attr;                      /* Attribute bits */
    char name[GPT_ENTRY_NAME_LENGTH];   /* Human readable name for partition */
} __attribute__((packed));

/* The gpt header */
struct gpt_header_t {
    char signature[TEST_GPT_SIG_LEN];   /* "EFI PART" */
    uint32_t revision;                  /* Revision number. */
    uint32_t size;                      /* Size of this header */
    uint32_t header_crc;                /* CRC of this header */
    uint32_t reserved;                  /* Reserved */
    uint64_t current_lba;               /* LBA of this header */
    uint64_t backup_lba;                /* LBA of backup GPT header */
    uint64_t first_lba;                 /* First usable LBA */
    uint64_t last_lba;                  /* Last usable LBA */
    struct efi_guid_t disk_guid;        /* Disk GUID */
    uint64_t array_lba;                 /* First LBA of array of partition entries */
    uint32_t num_partitions;            /* Number of partition entries in array */
    uint32_t entry_size;                /* Size of a single partition entry */
    uint32_t array_crc;                 /* CRC of partition entry array */
} __attribute__((packed));

static void register_mocked_read(void *buf, size_t num_bytes);
static ssize_t test_driver_read(uint64_t lba, void *buf);

/* LBA driver used in test module */
static struct gpt_flash_driver_t mock_driver = {
    .init = NULL,
    .uninit = NULL,
    .read = test_driver_read,
};

/* Valid MBR. Only signature is required to be valid */
static struct mbr_t default_mbr = {
    .unused = {0},
    .sig = TEST_MBR_SIG,
};
static struct mbr_t test_mbr;

/* Default GPT header. CRC values need to be populated to be valid. */
static struct gpt_header_t default_header = {
    .signature = TEST_GPT_SIG_INITIALISER,
    .revision = TEST_GPT_REVISION,
    .size = TEST_GPT_HEADER_SIZE,
    .header_crc = TEST_GPT_CRC32,
    .reserved = 0,
    .current_lba = TEST_GPT_PRIMARY_LBA,
    .backup_lba = TEST_GPT_BACKUP_LBA,
    .first_lba = TEST_GPT_FIRST_USABLE_LBA,
    .last_lba = TEST_GPT_LAST_USABLE_LBA,
    .disk_guid = TEST_GPT_DISK_GUID,
    .array_lba = TEST_GPT_ARRAY_LBA,
    .num_partitions = TEST_MAX_PARTITIONS,
    .entry_size = TEST_GPT_ENTRY_SIZE,
    .array_crc = TEST_GPT_CRC32
};
static struct gpt_header_t test_header;

/* Default entry array. This is valid, though fragmented. */
static struct gpt_entry_t default_partition_array[TEST_DEFAULT_NUM_PARTITIONS] = {
    {
        .type = MAKE_EFI_GUID(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .guid = MAKE_EFI_GUID(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .start = TEST_GPT_FIRST_PARTITION_START,
        .end = TEST_GPT_FIRST_PARTITION_END,
        .attr = 0,
        .name = "First partition"
    },
    {
        .type = MAKE_EFI_GUID(2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .guid = MAKE_EFI_GUID(2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .start = TEST_GPT_SECOND_PARTITION_START,
        .end = TEST_GPT_SECOND_PARTITION_END,
        .attr = 0,
        .name = "Second partition"
    },
    {
        .type = MAKE_EFI_GUID(3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .guid = MAKE_EFI_GUID(3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11),
        .start = TEST_GPT_THIRD_PARTITION_START,
        .end = TEST_GPT_THIRD_PARTITION_END,
        .attr = 0,
        .name = "Third partition"
    }
};
static struct gpt_entry_t test_partition_array[TEST_MAX_PARTITIONS];

/* Set to determine what is "read" by the flash driver. Allows for the mocking
 * of multiple read calls
 */
static unsigned int num_mocked_reads = 0;
static unsigned int registered_mocked_reads = 0;
static uint8_t mock_read_buffer[TEST_MOCK_BUFFER_SIZE][TEST_BLOCK_SIZE] = {0};

/* Turn ascii string to unicode */
static void ascii_to_unicode(const char *ascii, char *unicode)
{
    for (int i = 0; i < strlen(ascii) + 1; ++i) {
        unicode[i << 1] = ascii[i];
        unicode[(i << 1) + 1] = '\0';
    }
}

/* Tell the test what to return from the next read call. This is very much
 * whitebox testing
 */
static void register_mocked_read(void *data, size_t num_bytes)
{
    memcpy(mock_read_buffer[registered_mocked_reads++], data, num_bytes);
}

/* Driver function that always succeeds in reading the data it has been given */
static ssize_t test_driver_read(uint64_t lba, void *buf)
{
    memcpy(buf, mock_read_buffer[num_mocked_reads++], TEST_BLOCK_SIZE);
    return TEST_BLOCK_SIZE;
}

/* Creates backup table from test table and registers a read for it */
static void setup_backup_gpt(void)
{
    struct gpt_header_t backup_header;
    MAKE_BACKUP_HEADER(backup_header, test_header);

    register_mocked_read(&backup_header, sizeof(backup_header));
}

/* Uses the test MBR and GPT header to initialise for tests */
static psa_status_t setup_test_gpt(void)
{
    /* Expect first a valid MBR read */
    register_mocked_read(&test_mbr, sizeof(test_mbr));

    /* Expect a GPT header read second */
    register_mocked_read(&test_header, sizeof(test_header));

    /* Expect third each partition is read to find the number in use (if the
     * number in the header is non-zero)
     */
    if (test_header.num_partitions != 0) {
        register_mocked_read(&test_partition_array, sizeof(test_partition_array));
    }

    /* Expect fourth the backup to be read */
    setup_backup_gpt();

    return gpt_init(&mock_driver, TEST_MAX_PARTITIONS);
}

/* Ensures a valid GPT populated with the default entries is initialised */
static void setup_valid_gpt(void)
{
    TEST_ASSERT_EQUAL(PSA_SUCCESS, setup_test_gpt());
}

/* Ensures a valid but empty GPT is initialised */
static void setup_empty_gpt(void)
{
    test_header.num_partitions = 0;
    TEST_ASSERT_EQUAL(PSA_SUCCESS, setup_test_gpt());
}

void setUp(void)
{
    /* Default starting points */
    test_mbr = default_mbr;
    test_header = default_header;
    memcpy(&test_partition_array, &default_partition_array, sizeof(default_partition_array));
    for (size_t i = 0; i < TEST_DEFAULT_NUM_PARTITIONS; ++i) {
        char unicode_name[GPT_ENTRY_NAME_LENGTH] = {'\0'};
        ascii_to_unicode(test_partition_array[i].name, unicode_name);
        memcpy(test_partition_array[i].name, unicode_name, GPT_ENTRY_NAME_LENGTH);
    }

    test_mbr.partitions[0].os_type = TEST_MBR_TYPE_GPT;

    /* Ignore all logging calls */
    tfm_log_Ignore();

    num_mocked_reads = 0;
    registered_mocked_reads = 0;
    memset(mock_read_buffer, 0, sizeof(mock_read_buffer));
}

void tearDown(void)
{
    num_mocked_reads = 0;
    registered_mocked_reads = 0;
    memset(mock_read_buffer, 0, sizeof(mock_read_buffer));
    memset(&test_partition_array, 0, sizeof(test_partition_array));
    gpt_uninit();
}

void test_gpt_init_should_loadWhenGptGood(void)
{
    setup_valid_gpt();
}

void test_gpt_init_should_overwriteOldGpt(void)
{
    setup_valid_gpt();
    gpt_uninit();

    /* Use a different disk GUID */
    const struct efi_guid_t new_guid = MAKE_EFI_GUID(1, 1, 3, 4, 5, 6 ,7 ,8, 9, 10, 11);
    test_header.disk_guid = new_guid;

    setup_valid_gpt();
}

void test_gpt_init_should_failWhenMbrSigBad(void)
{
    test_mbr.sig--;
    TEST_ASSERT_EQUAL(PSA_ERROR_INVALID_ARGUMENT, setup_test_gpt());
}

void test_gpt_init_should_failWhenMbrTypeInvalid(void)
{
    test_mbr.partitions[0].os_type--;
    TEST_ASSERT_EQUAL(PSA_ERROR_NOT_SUPPORTED, setup_test_gpt());
}

void test_gpt_init_should_failWhenFlashDriverNotFullyDefined(void)
{
    gpt_flash_read_t read_fn = mock_driver.read;
    mock_driver.read = NULL;
    TEST_ASSERT_EQUAL(PSA_ERROR_INVALID_ARGUMENT, gpt_init(&mock_driver, TEST_MAX_PARTITIONS));
    mock_driver.read = read_fn;
}

void test_gpt_entry_read_should_populateEntry(void)
{
    /* Start with a populated GPT */
    setup_valid_gpt();

    /* Ensure an entry is found */
    struct partition_entry_t entry;
    struct gpt_entry_t *desired = &(test_partition_array[TEST_DEFAULT_NUM_PARTITIONS - 1]);
    struct efi_guid_t test_guid = desired->guid;
    struct efi_guid_t test_type = desired->type;
    register_mocked_read(&test_partition_array, sizeof(test_partition_array));
    TEST_ASSERT_EQUAL(PSA_SUCCESS, gpt_entry_read(&test_guid, &entry));

    /* Ensure this is the correct entry */
    TEST_ASSERT_EQUAL(0, efi_guid_cmp(&test_guid, &(entry.partition_guid)));
    TEST_ASSERT_EQUAL(0, efi_guid_cmp(&test_type, &(entry.type_guid)));
    TEST_ASSERT_EQUAL(desired->start, entry.start);

    /* Size is number of blocks, so subtract one */
    TEST_ASSERT_EQUAL(desired->end, entry.start + entry.size - 1);

    /* Name is unicode */
    TEST_ASSERT_EQUAL_MEMORY(desired->name, entry.name, GPT_ENTRY_NAME_LENGTH);
}

void test_gpt_entry_read_should_failWhenEntryNotExisting(void)
{
    /* Start with an empty GPT */
    setup_empty_gpt();

    /* Try to read something */
    struct partition_entry_t entry;
    struct efi_guid_t non_existing = NULL_GUID;
    TEST_ASSERT_EQUAL(PSA_ERROR_DOES_NOT_EXIST, gpt_entry_read(&non_existing, &entry));

    /* Now, have a non-empty GPT but search for a non-existing GUID */
    setup_valid_gpt();

    /* Each entry should be read */
    register_mocked_read(&test_partition_array, sizeof(test_partition_array));
    TEST_ASSERT_EQUAL(PSA_ERROR_DOES_NOT_EXIST, gpt_entry_read(&non_existing, &entry));
}
