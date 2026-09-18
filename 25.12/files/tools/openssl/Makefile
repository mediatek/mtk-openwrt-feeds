#
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 MediaTek Inc.
#
# Host-only OpenSSL, built solely to provide an ML-DSA (FIPS 204) capable
# libcrypto for the host mkimage/fit signing tools.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=openssl
PKG_VERSION:=3.5.8
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:= \
	http://www.openssl.org/source/ \
	http://www.openssl.org/source/old/$(basename $(PKG_VERSION))/ \
	https://github.com/openssl/openssl/releases/download/$(PKG_NAME)-$(PKG_VERSION)/

PKG_HASH:=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2

HOST_BUILD_PARALLEL:=1

# Private install directory to prevent overwriting the shared host LibreSSL.
OPENSSL_HOST_INSTALL_DIR:=$(STAGING_DIR_HOST)/openssl

include $(INCLUDE_DIR)/host-build.mk

define Host/Configure
	(cd $(HOST_BUILD_DIR); \
		./Configure \
			--prefix=$(OPENSSL_HOST_INSTALL_DIR) \
			--libdir=lib \
			no-shared \
			no-tests \
			-static && \
		{ [ -f $(STAMP_CONFIGURED) ] || make clean; } \
	)
endef

define Host/Compile
	$(call Host/Compile/Default,build_sw)
endef

define Host/Install
	$(INSTALL_DIR) $(OPENSSL_HOST_INSTALL_DIR)/lib $(OPENSSL_HOST_INSTALL_DIR)/include
	$(CP) $(HOST_BUILD_DIR)/libcrypto.a $(HOST_BUILD_DIR)/libssl.a \
		$(OPENSSL_HOST_INSTALL_DIR)/lib/
	$(CP) $(HOST_BUILD_DIR)/include/openssl $(OPENSSL_HOST_INSTALL_DIR)/include/
	$(CP) $(HOST_BUILD_DIR)/apps/openssl $(STAGING_DIR_HOST)/bin/openssl3
endef

define Host/Clean
	rm -rf $(OPENSSL_HOST_INSTALL_DIR)
endef

$(eval $(call HostBuild))
