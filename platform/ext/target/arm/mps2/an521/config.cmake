#-------------------------------------------------------------------------------
# SPDX-FileCopyrightText: Copyright The TrustedFirmware-M Contributors
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

include(hex_generator)

set(BL1                                 ON               CACHE BOOL      "Whether to build BL1")
set(PLATFORM_DEFAULT_BL1                ON               CACHE STRING    "Whether to use default BL1 or platform-specific one")
set(TFM_BL1_SOFTWARE_CRYPTO             ON               CACHE BOOL      "Whether BL1_1 will use software crypto")
set(TFM_BL1_MEMORY_MAPPED_FLASH         ON               CACHE BOOL      "Whether BL1 can directly access flash content")
set(TFM_BL1_2_IN_FLASH                  TRUE             CACHE BOOL      "Whether BL1_2 is stored in FLASH")
set(TFM_BL1_2_IN_OTP                    FALSE            CACHE BOOL      "Whether BL1_2 is stored in OTP")
set(TFM_BL1_2_IMAGE_ENCRYPTION          OFF              CACHE BOOL      "Whether BL1_2 will encrypt BL2 images")
set(TFM_BL1_2_ENABLE_ECDSA              ON               CACHE BOOL      "Enable ECDSA crypto for BL2 verification")
set(TFM_BL1_2_ENABLE_LMS                OFF              CACHE BOOL      "Enable LMS crypto for BL2 verification")
set(TFM_BL1_DEFAULT_PROVISIONING        ON               CACHE BOOL      "Whether BL1_1 will use default provisioning")
set(BL1_1_SHARED_SYMBOLS_PATH           ${CMAKE_SOURCE_DIR}/bl1/bl1_1/bl1_1_shared_symbols.txt CACHE FILEPATH "Path to list of BL1_1 shared symbols")

if(BL2)
    set(BL2_TRAILER_SIZE 0x10000 CACHE STRING "Trailer size")
else()
    #No header if no bootloader, but keep IMAGE_CODE_SIZE the same
    set(BL2_TRAILER_SIZE 0x10400 CACHE STRING "Trailer size")
endif()

# Platform-specific configurations

set(CONFIG_TFM_USE_TRUSTZONE          ON)
set(TFM_MULTI_CORE_TOPOLOGY           OFF)

set(PLATFORM_HAS_ISOLATION_L3_SUPPORT ON)

set(MCUBOOT_SIGNATURE_TYPE            "EC-P256"        CACHE STRING    "Algorithm to use for signature validation [RSA-2048, RSA-3072, EC-P256, EC-P384]")
set(MCUBOOT_HW_KEY                    OFF              CACHE BOOL      "Whether to embed the entire public key in the image metadata instead of the hash only")
set(MCUBOOT_BUILTIN_KEY               ON               CACHE BOOL      "Use builtin key(s) for validation, no public key data is embedded into the image metadata")

set(TFM_MERGE_HEX_FILES               ON                                              CACHE BOOL   "Create merged hex file in the end of the build")
set(TFM_S_HEX_FILE_PATH               "${CMAKE_BINARY_DIR}/bin/secure_fw.hex"         CACHE STRING "Merged secure hex file's path")

set(MCUBOOT_IMAGE_NUMBER                2           CACHE STRING    "Whether to combine S and NS into either 1 image, or sign each seperately")
