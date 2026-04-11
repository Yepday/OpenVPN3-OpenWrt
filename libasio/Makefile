include $(TOPDIR)/rules.mk

PKG_NAME:=libasio
PKG_VERSION:=1.30.2
PKG_RELEASE:=1

PKG_SOURCE:=asio-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=https://sourceforge.net/projects/asio/files/asio/$(PKG_VERSION)%20%28Stable%29
PKG_HASH:=9f12cef05c0477eace9c68ccabd19f9e3a04b875d4768c323714cbd3a5fa3c2b
PKG_SOURCE_SUBDIR:=asio-$(PKG_VERSION)
PKG_BUILD_DIR:=$(BUILD_DIR)/asio-$(PKG_VERSION)

PKG_MAINTAINER:=Yepday
PKG_LICENSE:=BSL-1.0
PKG_LICENSE_FILES:=LICENSE_1_0.txt
PKG_CPE_ID:=cpe:/a:think-async:asio

include $(INCLUDE_DIR)/package.mk

define Package/libasio
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Asio C++ Library (standalone, header-only)
  URL:=https://think-async.com/Asio/
endef

define Package/libasio/description
  Asio is a cross-platform C++ library for network and low-level I/O
  programming that provides a consistent asynchronous model using a modern
  C++ approach. This package provides the standalone (Boost-free) header-only
  version required by openvpn3.
endef

# Header-only: nothing to compile
define Build/Compile
endef

define Package/libasio/install
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/include/asio.hpp $(1)/usr/include/
	$(CP) -r $(PKG_BUILD_DIR)/include/asio $(1)/usr/include/
endef

# Install headers into staging_dir so other packages can find them at build time
define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/include/asio.hpp $(1)/usr/include/
	$(CP) -r $(PKG_BUILD_DIR)/include/asio $(1)/usr/include/
endef

$(eval $(call BuildPackage,libasio))
