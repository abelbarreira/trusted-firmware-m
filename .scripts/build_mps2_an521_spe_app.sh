#!/usr/bin/env bash

# Build the TF-M secure image and the tf-m-tests non-secure app for AN521,
# using the local uv-managed Python environment and the manually installed
# Arm GNU 14.2.Rel1 toolchain, then start the result in QEMU.
#
# Refer to https://trustedfirmware-m.readthedocs.io/en/latest/building/tfm_build_instruction.html#id7

SCRIPT_DIR=$(dirname "$0")
ROOT_DIR="$SCRIPT_DIR/.."

TFM_ROOT=$(realpath "$ROOT_DIR")
TFM_PLATFORM_SERIES="mps2"
TFM_PLATFORM_NAME="an521"
TFM_PLATFORM_PATH="arm/$TFM_PLATFORM_SERIES/$TFM_PLATFORM_NAME"
TFM_TESTS_REG_DIR=$(realpath "$TFM_ROOT/../tf-m-tests/tests_reg") # Checked out with lib/ext/tf-m-tests/version.txt
MCUBOOT_LOCAL_PATH=$(realpath "$TFM_ROOT/../mcuboot") # Checked out with config/config_base.cmake (tfm-2.3.0)
SPE_API_NS_DIR="$TFM_ROOT/build/api_ns"
NS_TOOLCHAIN_FILE="$SPE_API_NS_DIR/cmake/toolchain_ns_GNUARM.cmake"
PYTHON_BIN="$TFM_ROOT/.venv/bin/python3"
VENV_BIN_DIR="$TFM_ROOT/.venv/bin"
GNUARM_BIN_DIR="/opt/arm-gnu-toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin" # Refer to .scripts/setup_arm_gnu_toolchain.sh
CROSS_COMPILE_PREFIX="$GNUARM_BIN_DIR/arm-none-eabi"

set -e

pushd "$ROOT_DIR" > /dev/null

if [ ! -x "${CROSS_COMPILE_PREFIX}-gcc" ]; then
    echo "Missing Arm GNU toolchain compiler at: ${CROSS_COMPILE_PREFIX}-gcc"
    echo "Install Arm GNU Toolchain 14.2.Rel1 under:"
    echo "  $GNUARM_BIN_DIR"
    exit 1
fi

if [ ! -d "$MCUBOOT_LOCAL_PATH" ]; then
    echo "Missing local MCUboot checkout at: $MCUBOOT_LOCAL_PATH"
    exit 1
fi

if [ ! -d "$TFM_TESTS_REG_DIR" ]; then
    echo "Missing tf-m-tests at: $TFM_TESTS_REG_DIR"
    exit 1
fi

echo
echo Setup Python
.scripts/setup_python.sh
echo

export PATH="$VENV_BIN_DIR:$PATH"

echo
echo Config and Build SPE
echo

rm -rf build

cmake -S . -B build \
    -DTFM_PLATFORM="$TFM_PLATFORM_PATH" \
    -DMCUBOOT_IMAGE_NUMBER=1 \
    -DMCUBOOT_PATH="$MCUBOOT_LOCAL_PATH" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DTFM_BL2_LOG_LEVEL=LOG_LEVEL_INFO \
    -DTFM_SPM_LOG_LEVEL=LOG_LEVEL_INFO \
    -DTFM_PARTITION_LOG_LEVEL=LOG_LEVEL_INFO \
    -DCROSS_COMPILE="$CROSS_COMPILE_PREFIX" \
    -DPython3_EXECUTABLE="$PYTHON_BIN"

# build\api_ns subfolder
cmake --build build -- install

echo
echo Config and Build NS APP
echo

rm -rf build_test

cmake -S "$TFM_TESTS_REG_DIR" \
    -B build_test \
    -DCONFIG_SPE_PATH="$SPE_API_NS_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DTFM_TOOLCHAIN_FILE="$NS_TOOLCHAIN_FILE" \
    -DCROSS_COMPILE="$CROSS_COMPILE_PREFIX" \
    -DPython3_EXECUTABLE="$PYTHON_BIN"

cmake --build build_test

echo
echo Starting QEMU
echo

qemu-system-arm \
    -machine "$TFM_PLATFORM_SERIES-$TFM_PLATFORM_NAME" \
    -cpu cortex-m33 \
    -nographic \
    -monitor none \
    -serial stdio \
    -kernel build/api_ns/bin/bl2.axf \
    -device loader,file=build_test/tfm_s_ns_signed.bin,addr=0x10080000

popd > /dev/null

set +e
