#!/usr/bin/env bash

set -e

# Refer to
#  toolchain_GNUARM.cmake (secure-side toolchain file), and
#  platform/ns/toolchain_ns_GNUARM.cmake (NS-side toolchain file)
#
# So, recommended 14.2.Rel1.
#
# IMPORTANT:
#  It explicitly rejects exactly arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009, from config/check_config.cmake:145

# Download from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads: AArch32 bare-metal target (arm-none-eabi)
cd /tmp
wget https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo mkdir -p /opt/arm-gnu-toolchain
sudo tar -xJf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt/arm-gnu-toolchain

# Put it first on PATH
export PATH=/opt/arm-gnu-toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$PATH

# Verify
arm-none-eabi-gcc --version
which arm-none-eabi-gcc

rm arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz

cd -

set +e
