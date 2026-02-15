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

git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 defconfig
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/firmware/linux-firmware
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/kernel/linux/modules
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/iwinfo
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/wireless-tools
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/network/utils/iwinfo-ucode
git_sparse_clone 25.12-dev https://github.com/shiyu1314/immortalwrt-mt798x-6.12 package/mtk
git_sparse_clone openwrt-25.12 https://github.com/openwrt/openwrt target/linux/generic
git_sparse_clone openwrt-25.12 https://github.com/openwrt/openwrt target/linux/mediatek
git_sparse_clone openwrt-25.12 https://github.com/immortalwrt/immortalwrt package/firmware/wireless-regdb


rm -rf package/network/{utils/iwinfo,utils/wireless-tools}
rm -rf package/kernel/mt76
rm -rf target/linux/generic
rm -rf target/linux/mediatek
mv -v generic target/linux
mv -v mediatek target/linux
rm -rf package/firmware/linux-firmware
mv -v linux-firmware package/firmware
mv -v mtk package
mv -v {iwinfo,iwinfo-ucode,wireless-tools} package/network/utils


rm -rf package/kernel/linux/modules
mv -v modules package/kernel/linux

rm -rf package/firmware/wireless-regdb
mv -v wireless-regdb package/firmware

rm -rf package/mtk/applications/luci-app-mtwifi-cfg
rm -rf package/mtk/applications/luci-app-turboacc-mtk


rm -rf package/network/config/netifd/files/etc/init.d/packet_steering
rm -rf package/network/config/netifd/files/usr/libexec/network/packet-steering.uc

sed -i 's/^# \(CONFIG_\(WEXT_CORE\|WEXT_PRIV\|WEXT_PROC\|WEXT_SPY\|WIRELESS_EXT\)\) is not set$/\1=y/' target/linux/generic/config-6.12

curl -s https://raw.githubusercontent.com/shiyu1314/immortalwrt-mt798x-6.12/25.12-dev/package/kernel/linux/files/sysctl-nf-conntrack.conf > package/kernel/linux/files/sysctl-nf-conntrack.conf
curl -s https://raw.githubusercontent.com/shiyu1314/immortalwrt-mt798x-6.12/25.12-dev/target/linux/mediatek/filogic/target.mk > target/linux/mediatek/filogic/target.mk
curl -s https://raw.githubusercontent.com/shiyu1314/immortalwrt-mt798x-6.12/25.12-dev/target/linux/mediatek/filogic/config-6.12 > target/linux/mediatek/filogic/config-6.12

sed -i 's/libustream-mbedtls/libustream-openssl/' include/target.mk

sed -i "s/128/512/" package/base-files/files/bin/config_generate

rm -rf target/linux/mediatek/filogic/base-files/lib/preinit/90_extract_caldata

sed -i 's/ImmortalWrt-2.4G/OpenWrt-2.4G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/ImmortalWrt-5G/OpenWrt-5G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/ImmortalWrt-6G/OpenWrt-6G/' package/mtk/applications/mtwifi-cfg-ucode/files/lib/wifi/mtwifi.uc

sed -i 's/imply KERNEL_WERROR/# imply KERNEL_WERROR/' toolchain/gcc/Config.version

sed -i 's/^PKG_BUILD_PARALLEL:=1$/PKG_BUILD_PARALLEL:=1\nPKG_FORTIFY_SOURCE:=0/' package/libs/xcrypt/libxcrypt/Makefile
