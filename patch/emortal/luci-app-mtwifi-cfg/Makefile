#
# Copyright (C) hanwckf <hanwckf@vip.qq.com>
#
# This is free software, licensed under the GNU General Public License v2.
#

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI support for mtwifi-cfg
LUCI_DEPENDS:=+mtwifi-cfg-ucode
LUCI_PKGARCH:=all

PKG_VERSION:=1

define Package/luci-app-mtwifi-cfg/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_DIR) $(1)/etc/init.d

	$(INSTALL_BIN) ./root/usr/bin/mtk_auto_relay $(1)/usr/bin/
	$(INSTALL_BIN) ./root/etc/init.d/mtk_relay $(1)/etc/init.d/
endef

define Package/luci-app-mtwifi-cfg/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -f /tmp/luci-indexcache
	/etc/init.d/mtk_relay enable
	/etc/init.d/mtk_relay start
fi
exit 0
endef

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
