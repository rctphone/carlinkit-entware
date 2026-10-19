include $(TOPDIR)/rules.mk
PKG_NAME:=u2w-mfid
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=MIT
# src/ is the canonical source (mfid/build.sh builds the bench tools from it too).
PKG_FILE_DEPENDS:=$(CURDIR)/src $(CURDIR)/files
include $(INCLUDE_DIR)/package.mk

define Package/u2w-mfid
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=U2W MFi coprocessor over TCP for LIVI's MFi Host (USB NCM only)
endef

define Package/u2w-mfid/description
 mfid serves the dongle's MFi 2.0C coprocessor (i2c-1 at 0x10) in LIVI's wire
 (GET_CERT, SIGN, PROTO_MAJOR) so a Mac running LIVI can authenticate wired
 CarPlay. It listens on 192.168.7.1:5000 bound to the Mac-facing USB NCM
 interface (SO_BINDTODEVICE) and fails closed otherwise. No Wi-Fi, AP or
 network changes.
endef

define Package/u2w-mfid/conffiles
/etc/u2w-mfid.conf
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=gnu99 -Wall -Wextra \
		-o $(PKG_BUILD_DIR)/mfid $(PKG_BUILD_DIR)/mfid.c $(PKG_BUILD_DIR)/mfichip.c \
		$(TARGET_LDFLAGS)
endef

define Package/u2w-mfid/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mfid $(1)/usr/sbin/mfid
	$(INSTALL_BIN) ./files/u2w-mfid.init $(1)/etc/init.d/u2w-mfid
	$(INSTALL_CONF) ./files/u2w-mfid.conf $(1)/etc/u2w-mfid.conf
endef

$(eval $(call BuildPackage,u2w-mfid))
