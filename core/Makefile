# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release
.PHONY: print_debug_lib print_release_lib
.PHONY: print_debug_path print_release_path

#
# Required packages
#

PKGS = libglibutil glib-2.0 gobject-2.0

#
# Default target
#

all: debug release

#
# Library name
#

NAME = nfc-core
LIB_NAME = lib$(NAME)
LIB = $(LIB_NAME).a

#
# Sources
#

SRC = \
  nfc_adapter.c \
  nfc_crc.c \
  nfc_manager.c \
  nfc_ndef_rec.c \
  nfc_ndef_rec_u.c \
  nfc_plugins.c \
  nfc_plugin.c \
  nfc_tag.c \
  nfc_tag_t2.c \
  nfc_target.c \
  nfc_tlv.c \
  nfc_util.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release
COVERAGE_BUILD_DIR = $(BUILD_DIR)/coverage

#
# Pull nfcd version from nfc_version.h file
#

VERSION_FILE = $(INCLUDE_DIR)/nfc_version.h
get_version = $(shell grep -E "^ *\\\#define +NFC_VERSION_$1 +[0-9]+$$" $(VERSION_FILE) | sed "s/  */ /g" | cut -d " " -f 3)

VERSION_MAJOR = $(call get_version,MAJOR)
VERSION_MINOR = $(call get_version,MINOR)
VERSION_RELEASE = $(call get_version,NANO)

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wstrict-aliasing -Wunused-result
INCLUDES = -I$(INCLUDE_DIR)
BASE_FLAGS = -fPIC
FULL_CFLAGS = $(BASE_FLAGS) $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =
COVERAGE_FLAGS = -g

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
COVERAGE_CFLAGS = $(FULL_CFLAGS) $(COVERAGE_FLAGS) --coverage

#
# Files
#

PKGCONFIG_NAME = nfcd-plugin.pc
PKGCONFIG_TEMPLATE = $(PKGCONFIG_NAME).in
PKGCONFIG = $(BUILD_DIR)/$(PKGCONFIG_NAME)
DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
COVERAGE_OBJS = $(SRC:%.c=$(COVERAGE_BUILD_DIR)/%.o)


#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS) $(DEBUG_LIB): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS) $(RELEASE_LIB): | $(RELEASE_BUILD_DIR)
$(COVERAGE_OBJS) $(COVERAGE_LIB): | $(COVERAGE_BUILD_DIR)

#
# Rules
#

DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)
COVERAGE_LIB = $(COVERAGE_BUILD_DIR)/$(LIB)

debug: $(DEBUG_LIB)

release: $(RELEASE_LIB)

debug_lib: $(DEBUG_LIB)

release_lib: $(RELEASE_LIB)

coverage_lib: $(COVERAGE_LIB)

clean:
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~ $(INCLUDE_DIR)/internal/*~
	rm -fr $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_OBJS)
	$(AR) rc $@ $?
	ranlib $@

$(RELEASE_LIB): $(RELEASE_OBJS)
	$(AR) rc $@ $?
	ranlib $@

$(COVERAGE_LIB): $(COVERAGE_OBJS)
	$(AR) rc $@ $?
	ranlib $@

$(PKGCONFIG): $(PKGCONFIG_TEMPLATE) $(BUILD_DIR)
	sed -e "s/\[version\]/$(PCVERSION)/g" $(PKGCONFIG_TEMPLATE) > $@

#
# Install
#

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d

INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/nfcd
INSTALL_PKGCONFIG_DIR = $(DESTDIR)/usr/lib/pkgconfig

install: $(INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR) $(PKGCONFIG)
	$(INSTALL) -m 644 $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL) -m 644 $(PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
