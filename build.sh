#!/bin/bash
set -euo pipefail

# Path variables
DIR=$(readlink -f .)
MAIN=$(readlink -f ${DIR}/..)

# Set up ccache if not running in GitHub Actions
if [ -z "${GITHUB_ACTIONS-}" ]; then
    mkdir -p "$(pwd)/.ccache"
    export CCACHE_DIR="$(pwd)/.ccache"
fi
export USE_CCACHE=1

# Setup Clang
export CLANG_PATH=$MAIN/clang-tc/bin/
export PATH=${CLANG_PATH}:${PATH}
export CLANG_TRIPLE="aarch64-linux-gnu-"
export CROSS_COMPILE="aarch64-linux-gnu-"

# Config
THREAD="-j$(nproc --all)"
DEFCONFIG="pearl_defconfig"
export ARCH=arm64
export SUBARCH=$ARCH
export KBUILD_BUILD_USER=nobody
LLVM_CONFIG="LLVM=1 LLVM_IAS=1 AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip"

if ! grep -q "mtk_mmc_host" drivers/mmc/host/mtk-sd.c; then
    echo "/* 为 RPMB-MTK 驱动添加缺失的 mtk_mmc_host 定义 */" >> drivers/mmc/host/mtk-sd.c
    echo "struct mmc_host *mtk_mmc_host[MAX_MMC_HOST];" >> drivers/mmc/host/mtk-sd.c
    echo "EXPORT_SYMBOL(mtk_mmc_host);" >> drivers/mmc/host/mtk-sd.c
    echo "已添加 mtk_mmc_host 定义到 drivers/mmc/host/mtk-sd.c"
fi

if ! grep -q "mtk_mmc_host_init" drivers/mmc/host/mtk-sd.c; then
    echo "" >> drivers/mmc/host/mtk-sd.c
    echo "static int __init mtk_mmc_host_init(void)" >> drivers/mmc/host/mtk-sd.c
    echo "{" >> drivers/mmc/host/mtk-sd.c
    echo "    memset(mtk_mmc_host, 0, sizeof(mtk_mmc_host));" >> drivers/mmc/host/mtk-sd.c
    echo "    return 0;" >> drivers/mmc/host/mtk-sd.c
    echo "}" >> drivers/mmc/host/mtk-sd.c
    echo "arch_initcall(mtk_mmc_host_init);" >> drivers/mmc/host/mtk-sd.c
fi

# Build
make ${THREAD} CC="ccache clang" CXX="ccache clang++" ${LLVM_CONFIG} ${DEFCONFIG} O=out
make ${THREAD} CC="ccache clang" CXX="ccache clang++" ${LLVM_CONFIG} CONFIG_LTO_CLANG_THIN=y CONFIG_LTO_CLANG_FULL=n O=out 2>&1 | tee kernel.log
