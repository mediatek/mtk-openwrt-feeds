# SPDX-License-Identifier: MIT
#
# Copyright (C) 2026 MediaTek Inc.
#
# Author: Sky.Huang <SkyLake.Huang@mediatek.com>
#
# Distributed Llama - tensor-parallel LLM inference split across 1/2/4/8 nodes
# over plain Ethernet. Root node holds the model, workers get their slice pushed
# over TCP at startup. Same aarch64 / musl / ARMv8.0-A target as llama.cpp.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=distributed-llama
# Upstream tag v0.16.5 plus 6 fixes on main (incl. the wire-protocol bounds
# check, which matters because workers speak that protocol over the LAN).
PKG_VERSION:=0.16.5
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/b4rtaz/distributed-llama.git
PKG_SOURCE_VERSION:=59af889085c6c0316a4524a92b42f04caa4bcc6d
PKG_MIRROR_HASH:=e2d05f7670d456922b3f32fc999b008d9be5a5f372955861b0d2db1a7bb0c561

PKG_MAINTAINER:=Skylake Huang <skylake.huang@mediatek.com>
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/distributed-llama
  SECTION:=MTK Properties
  CATEGORY:=MTK Properties
  SUBMENU:=Edge LLM
  TITLE:=Distributed Llama (tensor-parallel CPU inference over Ethernet)
  URL:=https://github.com/b4rtaz/distributed-llama
  DEPENDS:= \
	+libstdcpp \
	+libpthread
endef

define Package/distributed-llama/description
  Tensor-parallel LLM inference across 1, 2, 4 or 8 nodes connected by
  Ethernet. Ships dllama (inference/chat/worker) and dllama-api
  (OpenAI-compatible server), plus a procd service for both roles.

  Only the root node needs the model; workers receive their slice from the
  root at startup. Models use Distributed Llama's own .m/.t format, not GGUF,
  and are NOT bundled - see /etc/config/dllama for the download URLs.
endef

define Package/distributed-llama/conffiles
/etc/config/dllama
endef

# Upstream ships a plain Makefile that hardcodes CXX=g++ and, off Termux,
# -march=native -mtune=native. Command-line assignments beat the ones inside a
# makefile, so overriding CXX/CXXFLAGS/LIBS here is enough - no patch needed.
# It also drops upstream's -Werror, which GCC 14 would otherwise trip over.
#
# Do NOT add -march/-mcpu: the toolchain default -mcpu=cortex-a53 leaves
# __ARM_FEATURE_DOTPROD undefined, so the sgemm dotprod kernels compile out ->
# plain NEON -> no SIGILL on Cortex-A53 or the MT7988's A73.
define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CXX="$(TARGET_CXX)" \
		CXXFLAGS="-std=c++11 -O3 $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS)" \
		LIBS="$(TARGET_LDFLAGS) -lpthread" \
		dllama dllama-api
endef

define Package/distributed-llama/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dllama $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dllama-api $(1)/usr/bin/

	$(INSTALL_BIN) ./files/etc/init.d/dllama $(1)/etc/init.d/dllama
	$(INSTALL_CONF) ./files/etc/config/dllama $(1)/etc/config/dllama
endef

$(eval $(call BuildPackage,distributed-llama))
