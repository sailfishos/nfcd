# -*- Mode: makefile-gmake -*-

.PHONY: all debug release clean
.PHONY: nfc_core_debug_lib nfc_core_release_lib
.PHONY: nfc_plugins_debug_lib nfc_plugins_release_lib

#
# Required packages
#

PKGS = gio-unix-2.0 gio-2.0 libglibutil
LIB_PKGS = $(PKGS) libdbusaccess

#
# Default target
#

all: debug release

#
# Sources
#

SRC = main.c

#
# Directories
#

SRC_DIR = .
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

LIBDIR ?= usr/lib
ABS_LIBDIR := $(shell echo /$(LIBDIR) | sed -r 's|/+|/|g')
DEFAULT_PLUGIN_DIR := $(ABS_LIBDIR)/nfcd/plugins

#
# libnfc-core
#

NFC_CORE_LIB = libnfc-core.a
NFC_CORE_DIR = ../core
NFC_CORE_BUILD_DIR = $(NFC_CORE_DIR)/build
NFC_CORE_DEBUG_LIB = $(NFC_CORE_BUILD_DIR)/debug/$(NFC_CORE_LIB)
NFC_CORE_RELEASE_LIB = $(NFC_CORE_BUILD_DIR)/release/$(NFC_CORE_LIB)

#
# libnfc-plugins
#

NFC_PLUGINS_LIB = libnfc-plugins.a
NFC_PLUGINS_DIR = ../plugins
NFC_PLUGINS_BUILD_DIR = $(NFC_PLUGINS_DIR)/build
NFC_PLUGINS_DEBUG_LIB = $(NFC_PLUGINS_BUILD_DIR)/debug/$(NFC_PLUGINS_LIB)
NFC_PLUGINS_RELEASE_LIB = $(NFC_PLUGINS_BUILD_DIR)/release/$(NFC_PLUGINS_LIB)

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
DEBUG_FLAGS = -g
RELEASE_FLAGS =
DEBUG_DEFS = -DDEBUG
RELEASE_DEFS =
WARNINGS = -Wall -Wstrict-aliasing -Wunused-result
DEFINES += -DDEFAULT_PLUGIN_DIR='"$(DEFAULT_PLUGIN_DIR)"'
INCLUDES = -I. -I$(NFC_CORE_DIR)/include -I$(NFC_PLUGINS_DIR)
FULL_CFLAGS = -fPIC $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(LDFLAGS) -Wl,-export-dynamic \
  -Wl,--version-script=$(SRC_DIR)/nfcd.map

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
SUBMAKE_OPTS += KEEP_SYMBOLS=1
endif

DEBUG_CFLAGS = $(DEBUG_FLAGS) -DDEBUG $(FULL_CFLAGS)
RELEASE_CFLAGS = $(RELEASE_FLAGS) -O2 $(FULL_CFLAGS)
DEBUG_LDFLAGS = $(DEBUG_FLAGS) $(FULL_LDFLAGS)
RELEASE_LDFLAGS = $(RELEASE_FLAGS) $(FULL_LDFLAGS)

LIBS = $(shell pkg-config --libs $(LIB_PKGS)) -ldl

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = \
  $(DEBUG_OBJS:%.o=%.d) \
  $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

DEBUG_DEPS = \
  nfc_core_debug_lib \
  nfc_plugins_debug_lib

DEBUG_EXE_DEPS = \
  $(NFC_CORE_DEBUG_LIB) \
  $(NFC_PLUGINS_DEBUG_LIB)

RELEASE_DEPS = \
  nfc_core_release_lib \
  nfc_plugins_release_lib

RELEASE_EXE_DEPS = \
  $(NFC_CORE_RELEASE_LIB) \
  $(NFC_PLUGINS_RELEASE_LIB)

$(NFC_CORE_DEBUG_LIB): | nfc_core_debug_lib
$(NFC_CORE_RELEASE_LIB): | nfc_core_release_lib
$(NFC_PLUGINS_DEBUG_LIB): | nfc_plugins_debug_lib
$(NFC_PLUGINS_RELEASE_LIB): | nfc_plugins_release_lib
$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

EXE = nfcd
DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: $(DEBUG_DEPS) $(DEBUG_EXE)

release: $(RELEASE_DEPS) $(RELEASE_EXE)

clean:
	$(MAKE) -C $(NFC_CORE_DIR) clean
	$(MAKE) -C $(NFC_PLUGINS_DIR) clean
	rm -fr $(BUILD_DIR) $(SRC_DIR)/*~

nfc_core_debug_lib:
	$(MAKE) -C $(NFC_CORE_DIR) debug

nfc_core_release_lib:
	$(MAKE) -C $(NFC_CORE_DIR) release

nfc_plugins_debug_lib:
	$(MAKE) -C $(NFC_PLUGINS_DIR) debug

nfc_plugins_release_lib:
	$(MAKE) -C $(NFC_PLUGINS_DIR) release

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_EXE_DEPS) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(NFC_PLUGINS_DEBUG_LIB) -Wl,--whole-archive $(NFC_CORE_DEBUG_LIB) -Wl,--no-whole-archive $(LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_EXE_DEPS) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(NFC_PLUGINS_RELEASE_LIB) -Wl,--whole-archive $(NFC_CORE_RELEASE_LIB) -Wl,--no-whole-archive $(LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

#
# Install
#

UNITDIR ?= usr/lib/systemd/system
ABS_UNITDIR := $(shell echo /$(UNITDIR) | sed -r 's|/+|/|g')

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d

INSTALL_SBIN_DIR = $(DESTDIR)/usr/sbin
INSTALL_SYSTEMD_DIR = $(DESTDIR)$(ABS_UNITDIR)

install: $(RELEASE_EXE) $(INSTALL_SBIN_DIR) $(INSTALL_SYSTEMD_DIR)
	$(INSTALL) -m 644 nfcd.service "$(INSTALL_SYSTEMD_DIR)"
	$(INSTALL) -m 755 $(RELEASE_EXE) "$(INSTALL_SBIN_DIR)"

$(INSTALL_SBIN_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_SYSTEMD_DIR):
	$(INSTALL_DIRS) $@
