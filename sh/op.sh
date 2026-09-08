#!/bin/bash

function git_sparse_clone() {
  branch="$1" repourl="$2" && shift 2
  git clone --depth=1 -b $branch --single-branch --filter=blob:none --sparse $repourl
  repodir=$(echo $repourl | awk -F '/' '{print $(NF)}')
  cd $repodir && git sparse-checkout set $@
  mv -f $@ ../
  cd .. && rm -rf $repodir
}

set -x

git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 defconfig
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/iwinfo
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/wireless-tools
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/iwinfo-ucode
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/config/wifi-scripts
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/mtk
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/firmware/wireless-regdb
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/kernel/airoha-phy-fw
git_sparse_clone 25.12 https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/kernel/as21xxx

mv -v airoha-phy-fw package/kernel
mv -v as21xxx package/kernel

rm -rf package/network/{utils/iwinfo,utils/wireless-tools}
mv -v mtk package
mv -v {iwinfo,iwinfo-ucode,wireless-tools} package/network/utils

rm -rf package/firmware/wireless-regdb
mv -v wireless-regdb package/firmware

rm -rf package/network/config/wifi-scripts
mv -v wifi-scripts package/network/config

rm -rf package/mtk/applications/luci-app-turboacc-mtk

sed -i 's/^# \(CONFIG_\(WEXT_CORE\|WEXT_PRIV\|WEXT_PROC\|WEXT_SPY\|WIRELESS_EXT\)\) is not set$/\1=y/' target/linux/generic/config-6.12

curl -s https://raw.githubusercontent.com/shiyu1314/immortalwrt-mt798x-6.12/25.12/target/linux/mediatek/filogic/target.mk > target/linux/mediatek/filogic/target.mk
curl -s https://raw.githubusercontent.com/shiyu1314/immortalwrt-mt798x-6.12/25.12/target/linux/mediatek/filogic/config-6.12 > target/linux/mediatek/filogic/config-6.12

sed -i 's/libustream-mbedtls/libustream-openssl/' include/target.mk

sed -i "s/128/512/" package/base-files/files/bin/config_generate

sed -i 's/ImmortalWrt-2.4G/OpenWrt-2.4G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/ImmortalWrt-5G/OpenWrt-5G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/ImmortalWrt-6G/OpenWrt-6G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/imply KERNEL_WERROR/# imply KERNEL_WERROR/' toolchain/gcc/Config.version

sed -i 's/^PKG_BUILD_PARALLEL:=1$/PKG_BUILD_PARALLEL:=1\nPKG_FORTIFY_SOURCE:=0/' package/libs/xcrypt/libxcrypt/Makefile

sed -i '/cmcc,rax3000m\*/a\\tcmcc,xr30* |\\' package/mtk/applications/mtk-smp/files/smp.sh

echo "MLREnable=0" >> package/mtk/drivers/wifi-profile/files/mt7981/mt7981.dbdc.b0.dat

echo "MLREnable=0" >> package/mtk/drivers/wifi-profile/files/mt7981/mt7981.dbdc.b1.dat

echo "ACK_CTS_TOUT_EN=1;1" >> package/mtk/drivers/wifi-profile/files/mt7981/mt7981.dbdc.b0.dat

echo "ACK_CTS_TOUT_EN=1;1" >> package/mtk/drivers/wifi-profile/files/mt7981/mt7981.dbdc.b1.dat

sed -i \
    -e 's/^PKG_VERSION:=.*/PKG_VERSION:=20250408/' \
    -e 's/^PKG_SOURCE:=.*/PKG_SOURCE:=mt79xx_conninfra_20250408-f2fa25.tar.xz/' \
    -e 's#^PKG_SOURCE_URL:=.*#PKG_SOURCE_URL:=https://raw.githubusercontent.com/immortalwrt-mt798x/immortalwrt/refs/heads/openwrt-21.02/dl#' \
    -e 's/^PKG_HASH:=.*/PKG_HASH:=3a82cfa69e68c8e584723b604dda565b2c085239ba1ac834d6d3b0ebd987e58c/' \
    package/mtk/drivers/conninfra/Makefile

for patch in *.patch; do
    [ -f "$patch" ] || continue
    
    echo "Applying $patch ..."
    patch -p1 --no-backup-if-mismatch < "$patch" || {
        echo "ERROR: Failed to apply $patch"
        popd
        exit 1
    }
done

rm -rf *.patch

