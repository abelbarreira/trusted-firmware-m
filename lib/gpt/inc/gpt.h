/*
 * SPDX-FileCopyrightText: Copyright The TrustedFirmware-M Contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __TFM_GPT_H__
#define __TFM_GPT_H__

#include <stdbool.h>
#include <stdint.h>

#include "psa/error.h"
#include "gpt_flash.h"
#include "efi_guid_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Name length of a GPT partition entry. */
#define GPT_ENTRY_NAME_LENGTH 72

/**
 * \brief Information about a GPT partition presented to callers.
 */
struct partition_entry_t {
    uint64_t start;                   /**< Start Logical Block Address (LBA) of the partition. */
    uint64_t size;                    /**< Size of the partition in number of LBAs. */
    char name[GPT_ENTRY_NAME_LENGTH]; /**< Human readable name for the partition in unicode. */
    uint64_t attr;                    /**< Attributes associated with the partition. */
    struct efi_guid_t partition_guid; /**< Unique partition GUID. */
    struct efi_guid_t type_guid;      /**< Partition type GUID. */
};

/**
 * \brief Reads the contents of a partition entry identified by a GUID.
 *
 * \param[in]  guid GUID of the partition entry to read.
 * \param[out] partition_entry Populated partition entry on success.
 *
 * \retval PSA_SUCCESS Success.
 * \retval PSA_ERROR_STORAGE_FAILURE I/O error.
 * \retval PSA_ERROR_DOES_NOT_EXIST No entry found with the provided GUID.
 */
__attribute__((nonnull(1,2)))
psa_status_t gpt_entry_read(const struct efi_guid_t  *guid,
                            struct partition_entry_t *partition_entry);

/**
 * \brief Reads the contents of a partition entry identified by name.
 *
 * \param[in]  name Name of the partition to read in unicode.
 * \param[in]  index Index to read when multiple entries share the same name.
 * \param[out] partition_entry Populated partition entry on success.
 *
 * \retval PSA_SUCCESS Success.
 * \retval PSA_ERROR_STORAGE_FAILURE I/O error.
 * \retval PSA_ERROR_DOES_NOT_EXIST No entry found with the provided name at \p index. For example,
 *                                  \p index was 1 (second entry) but only one entry was found.
 */
__attribute__((nonnull(1,3)))
psa_status_t gpt_entry_read_by_name(const char                name[GPT_ENTRY_NAME_LENGTH],
                                    const uint32_t            index,
                                    struct partition_entry_t *partition_entry);

/**
 * \brief Reads the contents of a partition entry identified by type.
 *
 * \param[in]  type Type of the partition to read.
 * \param[in]  index Index to read when multiple entries share the same type.
 * \param[out] partition_entry Populated partition entry on success.
 *
 * \retval PSA_SUCCESS Success.
 * \retval PSA_ERROR_STORAGE_FAILURE I/O error.
 * \retval PSA_ERROR_DOES_NOT_EXIST No entry found with the provided type at \p index. For example,
 *                                  \p index was 1 (second entry) but only one entry was found.
 */
__attribute__((nonnull(1,3)))
psa_status_t gpt_entry_read_by_type(const struct efi_guid_t  *type,
                                    const uint32_t            index,
                                    struct partition_entry_t *partition_entry);

/**
 * \brief Reads the GPT header from the second block (LBA 1).
 *
 * \param[in] flash_driver Driver used to perform I/O.
 * \param[in] max_partitions Maximum number of allowable partitions.
 *
 * \retval PSA_SUCCESS Success.
 * \retval PSA_ERROR_STORAGE_FAILURE I/O error.
 * \retval PSA_ERROR_INVALID_ARGUMENT \p max_partitions is less than four, or one of the I/O
 *                                    functions defined by \p flash_driver is NULL. The init
 *                                    and uninit functions may be NULL if not required.
 * \retval PSA_ERROR_NOT_SUPPORTED Legacy MBR is used and not GPT.
 */
__attribute__((nonnull(1)))
psa_status_t gpt_init(struct gpt_flash_driver_t *flash_driver,
                      uint64_t max_partitions);

/**
 * \brief Uninitialises the library.
 *
 * \retval PSA_SUCCESS Success.
 * \retval PSA_ERROR_STORAGE_FAILURE I/O error.
 */
psa_status_t gpt_uninit(void);

#ifdef __cplusplus
}
#endif

#endif /* __TFM_GPT_H__ */
