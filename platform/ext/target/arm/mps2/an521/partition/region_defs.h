/*
 * Copyright (c) 2017-2022 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __REGION_DEFS_H__
#define __REGION_DEFS_H__

#include "flash_layout.h"

#define BL1_1_HEAP_SIZE         (0x00001000)
#define BL1_1_MSP_STACK_SIZE    (0x00001800)

#define BL1_2_HEAP_SIZE         (0x00001000)
#define BL1_2_MSP_STACK_SIZE    (0x00001800)

#define BL2_HEAP_SIZE           (0x00001000)
#define BL2_MSP_STACK_SIZE      (0x00001800)

#ifdef ENABLE_HEAP
#define S_HEAP_SIZE             (0x00000200)
#endif

#ifdef TFM_FIH_PROFILE_ON
#define S_MSP_STACK_SIZE        (0x00000A40)
#else
#define S_MSP_STACK_SIZE        (0x00000800)
#endif
#define S_PSP_STACK_SIZE        (0x00000800)

#define NS_HEAP_SIZE            (0x00001000)
#define NS_STACK_SIZE           (0x000001E0)

/* MPC granularity is 128 KB on AN521 Castor MPS2 FPGA image. Alignment
 * of partitions is defined in accordance with this constraint.
 */

#ifdef BL2
#ifndef LINK_TO_SECONDARY_PARTITION
#define S_IMAGE_PRIMARY_PARTITION_OFFSET   (FLASH_AREA_0_OFFSET)
#define S_IMAGE_SECONDARY_PARTITION_OFFSET (FLASH_AREA_2_OFFSET)
#else
#define S_IMAGE_PRIMARY_PARTITION_OFFSET   (FLASH_AREA_2_OFFSET)
#define S_IMAGE_SECONDARY_PARTITION_OFFSET (FLASH_AREA_0_OFFSET)
#endif /* !LINK_TO_SECONDARY_PARTITION */
#else
#define S_IMAGE_PRIMARY_PARTITION_OFFSET (0x0)
#endif /* BL2 */

#ifndef LINK_TO_SECONDARY_PARTITION
#define NS_IMAGE_PRIMARY_PARTITION_OFFSET (FLASH_AREA_0_OFFSET \
                                           + FLASH_S_PARTITION_SIZE)
#else
#define NS_IMAGE_PRIMARY_PARTITION_OFFSET (FLASH_AREA_2_OFFSET \
                                           + FLASH_S_PARTITION_SIZE)
#endif /* !LINK_TO_SECONDARY_PARTITION */

/* Boot partition structure if MCUBoot is used:
 * 0x0_0000 Bootloader header
 * 0x0_0400 Image area
 * 0x7_0000 Trailer
 */
/* IMAGE_CODE_SIZE is the space available for the software binary image.
 * It is less than the FLASH_S_PARTITION_SIZE + FLASH_NS_PARTITION_SIZE
 * because we reserve space for the image header and trailer introduced
 * by the bootloader.
 */

#if (!defined(MCUBOOT_IMAGE_NUMBER) || (MCUBOOT_IMAGE_NUMBER == 1)) && \
    (NS_IMAGE_PRIMARY_PARTITION_OFFSET > S_IMAGE_PRIMARY_PARTITION_OFFSET)
/* If secure image and nonsecure image are concatenated, and nonsecure image
 * locates at the higher memory range, then the secure image does not need
 * the trailer area.
 */
#define IMAGE_S_CODE_SIZE \
            (FLASH_S_PARTITION_SIZE - BL2_HEADER_SIZE)
#else
#define IMAGE_S_CODE_SIZE \
            (FLASH_S_PARTITION_SIZE - BL2_HEADER_SIZE - BL2_TRAILER_SIZE)
#endif

#define IMAGE_NS_CODE_SIZE \
            (FLASH_NS_PARTITION_SIZE - BL2_HEADER_SIZE - BL2_TRAILER_SIZE)

/* Alias definitions for secure and non-secure areas*/
#define S_ROM_ALIAS(x)  (S_ROM_ALIAS_BASE + (x))
#define NS_ROM_ALIAS(x) (NS_ROM_ALIAS_BASE + (x))

#define S_RAM_ALIAS(x)  (S_RAM_ALIAS_BASE + (x))
#define NS_RAM_ALIAS(x) (NS_RAM_ALIAS_BASE + (x))

/* Shared data area between boot stages and runtime firmware.
 * Keep this at the start of secure SRAM so later stages can reuse the existing
 * TF-M handoff convention.
 */
#define BOOT_TFM_SHARED_DATA_BASE  (S_RAM_ALIAS(0x0))
#define BOOT_TFM_SHARED_DATA_SIZE  (0x400)
#define BOOT_TFM_SHARED_DATA_LIMIT (BOOT_TFM_SHARED_DATA_BASE + \
                                    BOOT_TFM_SHARED_DATA_SIZE - 1)
#define SHARED_BOOT_MEASUREMENT_BASE  BOOT_TFM_SHARED_DATA_BASE
#define SHARED_BOOT_MEASUREMENT_SIZE  BOOT_TFM_SHARED_DATA_SIZE
#define SHARED_BOOT_MEASUREMENT_LIMIT BOOT_TFM_SHARED_DATA_LIMIT

#ifdef BL1
#define BL1_1_CODE_START  (S_ROM_ALIAS(FLASH_AREA_BL1_1_OFFSET))
#define BL1_1_CODE_SIZE   (FLASH_AREA_BL1_1_SIZE)
#define BL1_1_CODE_LIMIT  (BL1_1_CODE_START + BL1_1_CODE_SIZE - 1)

#define BL1_2_CODE_START  (S_RAM_ALIAS(0x100000))
#define BL1_2_CODE_SIZE   (FLASH_AREA_BL1_2_SIZE)
#define BL1_2_CODE_LIMIT  (BL1_2_CODE_START + BL1_2_CODE_SIZE - 1)

#define BL1_1_DATA_START  (BOOT_TFM_SHARED_DATA_BASE + BOOT_TFM_SHARED_DATA_SIZE)
#define BL1_1_DATA_SIZE   (0x00004000)
#define BL1_1_DATA_LIMIT  (BL1_1_DATA_START + BL1_1_DATA_SIZE - 1)

#define BL1_2_DATA_START  (S_RAM_ALIAS(0x108000))
#define BL1_2_DATA_SIZE   (0x00004000)
#define BL1_2_DATA_LIMIT  (BL1_2_DATA_START + BL1_2_DATA_SIZE - 1)
#endif /* BL1 */

/* Secure regions */
#define S_IMAGE_PRIMARY_AREA_OFFSET \
             (S_IMAGE_PRIMARY_PARTITION_OFFSET + BL2_HEADER_SIZE)
#define S_CODE_START    (S_ROM_ALIAS(S_IMAGE_PRIMARY_AREA_OFFSET))
#define S_CODE_SIZE     (IMAGE_S_CODE_SIZE)
#define S_CODE_LIMIT    (S_CODE_START + S_CODE_SIZE - 1)

/* Size of vector table: 139 interrupt handlers + 4 bytes MPS initial value */
#define S_CODE_VECTOR_TABLE_SIZE    (0x230)

#define S_DATA_START    (S_RAM_ALIAS(0x0))
#define S_DATA_SIZE     (TOTAL_RAM_SIZE / 2)
#define S_DATA_LIMIT    (S_DATA_START + S_DATA_SIZE - 1)

/* Non-secure regions */
#define NS_IMAGE_PRIMARY_AREA_OFFSET \
                        (NS_IMAGE_PRIMARY_PARTITION_OFFSET + BL2_HEADER_SIZE)
#define NS_CODE_START   (NS_ROM_ALIAS(NS_IMAGE_PRIMARY_AREA_OFFSET))
#define NS_CODE_SIZE    (IMAGE_NS_CODE_SIZE)
#define NS_CODE_LIMIT   (NS_CODE_START + NS_CODE_SIZE - 1)

#define NS_DATA_START   (NS_RAM_ALIAS(TOTAL_RAM_SIZE / 2))
#if defined(PSA_API_TEST_NS) && !defined(PSA_API_TEST_IPC)
#define DEV_APIS_TEST_NVMEM_REGION_SIZE  0x400
#define NS_DATA_SIZE    (TOTAL_RAM_SIZE / 2 - DEV_APIS_TEST_NVMEM_REGION_SIZE)
#else
#define NS_DATA_SIZE    (TOTAL_RAM_SIZE / 2)
#endif
#define NS_DATA_LIMIT   (NS_DATA_START + NS_DATA_SIZE - 1)

/* NS partition information is used for MPC and SAU configuration */
#define NS_PARTITION_START \
            (NS_ROM_ALIAS(NS_IMAGE_PRIMARY_PARTITION_OFFSET))
#define NS_PARTITION_SIZE (FLASH_NS_PARTITION_SIZE)

/* Secondary partition for new images in case of firmware upgrade */
#define SECONDARY_PARTITION_START \
            (NS_ROM_ALIAS(S_IMAGE_SECONDARY_PARTITION_OFFSET))
#define SECONDARY_PARTITION_SIZE (FLASH_S_PARTITION_SIZE + \
                                  FLASH_NS_PARTITION_SIZE)

#ifdef BL2
/* Bootloader regions */
/* BL2 is stored in flash but executed from secure SRAM when BL1 is enabled. */
#define BL2_CODE_START    (S_RAM_ALIAS(0x1000))
#define BL2_CODE_SIZE     (FLASH_AREA_BL2_SIZE)
#define BL2_CODE_LIMIT    (BL2_CODE_START + BL2_CODE_SIZE - 1)

#define BL2_DATA_START    (S_RAM_ALIAS(0x81000))
#define BL2_DATA_SIZE     (TOTAL_RAM_SIZE - 0x81000)
#define BL2_DATA_LIMIT    (BL2_DATA_START + BL2_DATA_SIZE - 1)

#define BL2_IMAGE_LOAD_ADDRESS (S_ROM_ALIAS(FLASH_AREA_BL2_OFFSET))
#endif /* BL2 */

/* Shared symbol area between bootloader and runtime firmware. Global variables
 * in the shared code can be placed here.
 */
#ifdef CODE_SHARING
#define SHARED_SYMBOL_AREA_BASE S_RAM_ALIAS_BASE
#define SHARED_SYMBOL_AREA_SIZE 0x20
#else
#define SHARED_SYMBOL_AREA_BASE S_RAM_ALIAS_BASE
#define SHARED_SYMBOL_AREA_SIZE 0x0
#endif /* CODE_SHARING */

#endif /* __REGION_DEFS_H__ */
