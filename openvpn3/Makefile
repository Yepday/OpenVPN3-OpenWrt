include $(TOPDIR)/rules.mk

PKG_NAME:=openvpn3-openwrt
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

# openvpn3-core (header-only SDK) — cloned at build time from upstream master.
# Tested against commit from 2026-04 (core 3.12_git:master).
# Pin to a specific commit for reproducible builds, or use master for latest.
OPENVPN3_CORE_REPO:=https://github.com/OpenVPN/openvpn3.git
OPENVPN3_CORE_BRANCH:=master
OPENVPN3_CORE_DIR:=$(BUILD_DIR)/openvpn3-core

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_MAINTAINER:=Yepday
PKG_LICENSE:=AGPL-3.0-only
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_DEPENDS:=libasio openssl jsoncpp libfmt liblz4 xxhash

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/openvpn3-openwrt
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=VPN
  TITLE:=OpenVPN 3 Client
  URL:=https://github.com/OpenVPN/openvpn3
  DEPENDS:=+libopenssl +liblz4 +jsoncpp +xxhash +libfmt +libstdcpp +kmod-tun +libubus +libubox +libuci
endef

define Package/openvpn3-openwrt/description
  OpenVPN 3 is a C++20 client-only VPN library implementing the OpenVPN
  protocol. This package provides an OpenWrt-native client daemon with
  procd service management, ubus control interface, netifd integration,
  and UCI-based configuration. Accepts standard .ovpn profile files.
endef

define Build/Prepare
	# Clone openvpn3-core if not already present
	if [ ! -d "$(OPENVPN3_CORE_DIR)" ]; then \
		git clone --depth=1 --branch $(OPENVPN3_CORE_BRANCH) \
			$(OPENVPN3_CORE_REPO) $(OPENVPN3_CORE_DIR); \
	fi
	mkdir -p $(PKG_BUILD_DIR)
	ln -sfn $(CURDIR)/src $(PKG_BUILD_DIR)/src
	ln -sfn $(CURDIR)/CMakeLists.txt $(PKG_BUILD_DIR)/CMakeLists.txt
endef

CMAKE_OPTIONS += \
	-DUSE_MBEDTLS=OFF \
	-DBUILD_TESTING=OFF \
	-DCLI_OVPNDCO=OFF \
	-DOPENVPN3_CORE_DIR=$(OPENVPN3_CORE_DIR)

define Package/openvpn3-openwrt/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/openvpn3d $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(CURDIR)/files/openvpn3.init $(1)/etc/init.d/openvpn3
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(CURDIR)/files/openvpn3.config $(1)/etc/config/openvpn3
	$(INSTALL_DIR) $(1)/etc/openvpn3
endef

$(eval $(call BuildPackage,openvpn3-openwrt))
