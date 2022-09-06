# -*- Mode: makefile-gmake -*-

.PHONY: all debug release clean install
.PHONY: nfc_core_debug_lib nfc_core_release_lib

#
# Required packages
#

PKGS = gio-unix-2.0 gio-2.0 libglibutil
LIB_PKGS = $(PKGS)

#
# Default target
#

all: debug release

#
# Sources
#

SRC = ndef-read.c

GEN_SRC = \
  org.sailfishos.nfc.Adapter.c \
  org.sailfishos.nfc.Daemon.c \
  org.sailfishos.nfc.NDEF.c \
  org.sailfishos.nfc.Tag.c

ALL_SRC = $(GEN_SRC) $(SRC)

#
# Directories
#

SRC_DIR = .
BUILD_DIR = build
SPEC_DIR = ../../plugins/dbus_service
GEN_DIR = $(BUILD_DIR)
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# libnfc-core
#

NFC_CORE_LIB = libnfc-core.a
NFC_CORE_DIR = ../../core
NFC_CORE_BUILD_DIR = $(NFC_CORE_DIR)/build
NFC_CORE_DEBUG_LIB = $(NFC_CORE_BUILD_DIR)/debug/$(NFC_CORE_LIB)
NFC_CORE_RELEASE_LIB = $(NFC_CORE_BUILD_DIR)/release/$(NFC_CORE_LIB)

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
INCLUDES = -I. -I$(NFC_CORE_DIR)/include -I$(GEN_DIR)
FULL_CFLAGS = -fPIC $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(LDFLAGS)

KEEP_SYMBOLS ?= 0
ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
SUBMAKE_OPTS += KEEP_SYMBOLS=1
endif

DEBUG_CFLAGS = $(DEBUG_FLAGS) -DDEBUG $(FULL_CFLAGS)
RELEASE_CFLAGS = $(RELEASE_FLAGS) -O2 $(FULL_CFLAGS)
DEBUG_LDFLAGS = $(DEBUG_FLAGS) $(FULL_LDFLAGS)
RELEASE_LDFLAGS = $(RELEASE_FLAGS) $(FULL_LDFLAGS)

LIBS = $(shell pkg-config --libs $(LIB_PKGS)) -ldl
DEBUG_LIBS = \
  $(NFC_CORE_DEBUG_LIB) \
  $(LIBS)
RELEASE_LIBS = \
  $(NFC_CORE_RELEASE_LIB) \
  $(LIBS)

#
# Files
#

DEBUG_OBJS = $(ALL_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(ALL_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

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

DEBUG_DEPS = nfc_core_debug_lib
RELEASE_DEPS = nfc_core_release_lib

DEBUG_EXE_DEPS = $(NFC_CORE_DEBUG_LIB)
RELEASE_EXE_DEPS = $(NFC_CORE_RELEASE_LIB)

$(NFC_CORE_DEBUG_LIB): | nfc_core_debug_lib
$(NFC_CORE_RELEASE_LIB): | nfc_core_release_lib
$(GEN_SRC:%=$(GEN_DIR)/%): | $(GEN_DIR)
$(SRC): | $(GEN_SRC:%=$(GEN_DIR)/%)
$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

EXE = ndef-read
DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: $(DEBUG_DEPS) $(DEBUG_EXE)

release: $(RELEASE_DEPS) $(RELEASE_EXE)

clean:
	rm -fr $(BUILD_DIR) $(SRC_DIR)/*~

nfc_core_debug_lib:
	$(MAKE) -C $(NFC_CORE_DIR) debug

nfc_core_release_lib:
	$(MAKE) -C $(NFC_CORE_DIR) release

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_EXE_DEPS) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_EXE_DEPS) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

#
# Install
#

INSTALL = install
INSTALL_BIN_DIR = $(DESTDIR)/usr/bin

install: $(INSTALL_BIN_DIR)
	$(INSTALL) -m 755 $(RELEASE_EXE) $(INSTALL_BIN_DIR)

$(INSTALL_BIN_DIR):
	$(INSTALL) -d $@
