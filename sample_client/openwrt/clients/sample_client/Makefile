include $(TOPDIR)/rules.mk

# Name, version and release number
# The name and version of your package are used to define the variable to point to the build directory of your package: $(PKG_BUILD_DIR)
PKG_NAME:=sample_client
PKG_VERSION:=1.0
PKG_RELEASE:=1

# Project settings (i.e. where to find the source codes)
# This is a custom variable, used below, should be changed according to your project location
PROJECT_DIR:=~/embedded-client

include $(INCLUDE_DIR)/package.mk

# Package definition; instructs on how and where our package will appear in the overall configuration menu ('make menuconfig')
define Package/sample_client
  SECTION:=clients
  CATEGORY:=Clients
  TITLE:=Sample client
endef

# Package description; a more verbose description on what our package does
define Package/sample_client/description
  Sample client
endef

# Package preparation instructions; create the build directory and copy the source code.
# The last command is necessary to ensure our preparation instructions remain compatible with the patching system.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -rf $(PROJECT_DIR) $(PKG_BUILD_DIR)
	$(Build/Patch)
endef

# Package build instructions; invoke the target-specific compiler to first compile the source file, and then to link the file into the final executable
define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/embedded-client CC="$(TARGET_CC)"
	$(MAKE) -C $(PKG_BUILD_DIR)/embedded-client/sample_client CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

# Package install instructions; create a directory inside the package to hold our executable, and then copy the executable we built previously into the folder
define Package/sample_client/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/embedded-client/sample_client $(1)/usr/bin
endef

# This command is always the last, it uses the definitions and variables we give above in order to get the job done
$(eval $(call BuildPackage,sample_client))