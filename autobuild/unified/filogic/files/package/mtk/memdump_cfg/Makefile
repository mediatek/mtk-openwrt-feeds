#
# Copyright (C) 2008-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=memdump-cfg
PKG_RELEASE:=3
PKG_LICENSE:=BSD-3-Clause

include $(INCLUDE_DIR)/package.mk

define KernelPackage/memdump-cfg
  SUBMENU:=Other modules
  TITLE:=Configuration helper for memdump
  DEFAULT:=y if TARGET_mediatek
  FILES:=$(PKG_BUILD_DIR)/memdump-cfg.ko
  AUTOLOAD:=$(call AutoLoad,10,memdump-cfg,1)
  KCONFIG:=
endef

define KernelPackage/memdump-cfg/description
  This is used to configure the memdump function
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,memdump-cfg))
