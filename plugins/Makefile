# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release coverage

#
# Required packages
#

PKGS = libglibutil glib-2.0 gobject-2.0 gio-2.0 gio-unix-2.0

#
# libdbusaccess is optional
#

HAVE_DBUSACCESS ?= 1

ifneq ($(HAVE_DBUSACCESS),0)
PKGS += libdbusaccess
DEFINES += -DHAVE_DBUSACCESS
endif

#
# Default target
#

all: debug release

#
# Library name
#

NAME = nfc-plugins
LIB_NAME = lib$(NAME)
LIB = $(LIB_NAME).a

#
# Directories
#

BUILD_DIR = build
GEN_DIR = $(BUILD_DIR)/gen
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release
COVERAGE_BUILD_DIR = $(BUILD_DIR)/coverage

#
# D-Bus org.sailfishos.nfc plugin
#

DBUS_SERVICE_DIR = dbus_service
DBUS_SERVICE_PLUGIN_SRC = \
  dbus_service_adapter.c \
  dbus_service_error.c \
  dbus_service_isodep.c \
  dbus_service_local.c \
  dbus_service_name.c \
  dbus_service_ndef.c \
  dbus_service_peer.c \
  dbus_service_plugin.c \
  dbus_service_util.c \
  dbus_service_tag.c \
  dbus_service_tag_t2.c

DBUS_SERVICE_GEN_SRC = \
  org.sailfishos.nfc.Adapter.c \
  org.sailfishos.nfc.Daemon.c \
  org.sailfishos.nfc.IsoDep.c \
  org.sailfishos.nfc.LocalService.c \
  org.sailfishos.nfc.NDEF.c \
  org.sailfishos.nfc.Peer.c \
  org.sailfishos.nfc.Tag.c \
  org.sailfishos.nfc.TagType2.c

DBUS_SERVICE_SRC = \
  $(DBUS_SERVICE_GEN_SRC) \
  $(DBUS_SERVICE_PLUGIN_SRC)

DBUS_SERVICE_GEN_DIR = $(GEN_DIR)/$(DBUS_SERVICE_DIR)
DEBUG_DBUS_SERVICE_BUILD_DIR = $(DEBUG_BUILD_DIR)/$(DBUS_SERVICE_DIR)
RELEASE_DBUS_SERVICE_BUILD_DIR = $(RELEASE_BUILD_DIR)/$(DBUS_SERVICE_DIR)
COVERAGE_DBUS_SERVICE_BUILD_DIR = $(COVERAGE_BUILD_DIR)/$(DBUS_SERVICE_DIR)

DEBUG_DBUS_SERVICE_OBJS = $(DBUS_SERVICE_SRC:%.c=$(DEBUG_DBUS_SERVICE_BUILD_DIR)/%.o)
RELEASE_DBUS_SERVICE_OBJS = $(DBUS_SERVICE_SRC:%.c=$(RELEASE_DBUS_SERVICE_BUILD_DIR)/%.o)
COVERAGE_DBUS_SERVICE_OBJS = $(DBUS_SERVICE_SRC:%.c=$(COVERAGE_DBUS_SERVICE_BUILD_DIR)/%.o)

DEBUG_OBJS += $(DEBUG_DBUS_SERVICE_OBJS)
RELEASE_OBJS += $(RELEASE_DBUS_SERVICE_OBJS)
COVERAGE_OBJS += $(COVERAGE_DBUS_SERVICE_OBJS)

$(DBUS_SERVICE_GEN_SRC:%=$(DBUS_SERVICE_GEN_DIR)/%): | $(DBUS_SERVICE_GEN_DIR)
$(DBUS_SERVICE_PLUGIN_SRC:%=$(DBUS_SERVICE_DIR)/%): | $(DBUS_SERVICE_GEN_SRC:%=$(DBUS_SERVICE_GEN_DIR)/%)
$(DEBUG_DBUS_SERVICE_OBJS): | $(DEBUG_DBUS_SERVICE_BUILD_DIR)
$(RELEASE_DBUS_SERVICE_OBJS): | $(RELEASE_DBUS_SERVICE_BUILD_DIR)
$(COVERAGE_DBUS_SERVICE_OBJS): | $(COVERAGE_DBUS_SERVICE_BUILD_DIR)

$(DBUS_SERVICE_GEN_DIR):
	mkdir -p $@

$(DEBUG_DBUS_SERVICE_BUILD_DIR):
	mkdir -p $@

$(RELEASE_DBUS_SERVICE_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_DBUS_SERVICE_BUILD_DIR):
	mkdir -p $@

#
# D-Bus org.neard plugin
#

DBUS_NEARD_DIR = dbus_neard
DBUS_NEARD_PLUGIN_SRC = \
  dbus_neard.c \
  dbus_neard_adapter.c \
  dbus_neard_error.c \
  dbus_neard_plugin.c \
  dbus_neard_tag.c

DBUS_NEARD_GEN_SRC = \
  org.neard.Adapter.c \
  org.neard.Record.c \
  org.neard.Tag.c

DBUS_NEARD_SRC = \
  $(DBUS_NEARD_GEN_SRC) \
  $(DBUS_NEARD_PLUGIN_SRC)

DBUS_NEARD_GEN_DIR = $(GEN_DIR)/$(DBUS_NEARD_DIR)
DEBUG_DBUS_NEARD_BUILD_DIR = $(DEBUG_BUILD_DIR)/$(DBUS_NEARD_DIR)
RELEASE_DBUS_NEARD_BUILD_DIR = $(RELEASE_BUILD_DIR)/$(DBUS_NEARD_DIR)
COVERAGE_DBUS_NEARD_BUILD_DIR = $(COVERAGE_BUILD_DIR)/$(DBUS_NEARD_DIR)

DEBUG_DBUS_NEARD_OBJS = $(DBUS_NEARD_SRC:%.c=$(DEBUG_DBUS_NEARD_BUILD_DIR)/%.o)
RELEASE_DBUS_NEARD_OBJS = $(DBUS_NEARD_SRC:%.c=$(RELEASE_DBUS_NEARD_BUILD_DIR)/%.o)
COVERAGE_DBUS_NEARD_OBJS = $(DBUS_NEARD_SRC:%.c=$(COVERAGE_DBUS_NEARD_BUILD_DIR)/%.o)

DEBUG_OBJS += $(DEBUG_DBUS_NEARD_OBJS)
RELEASE_OBJS += $(RELEASE_DBUS_NEARD_OBJS)
COVERAGE_OBJS += $(COVERAGE_DBUS_NEARD_OBJS)

$(DBUS_NEARD_GEN_SRC:%=$(DBUS_NEARD_GEN_DIR)/%): | $(DBUS_NEARD_GEN_DIR)
$(DBUS_NEARD_PLUGIN_SRC:%=$(DBUS_NEARD_DIR)/%):| $(DBUS_NEARD_GEN_SRC:%=$(DBUS_NEARD_GEN_DIR)/%)
$(DEBUG_DBUS_NEARD_OBJS): | $(DEBUG_DBUS_NEARD_BUILD_DIR)
$(RELEASE_DBUS_NEARD_OBJS): | $(RELEASE_DBUS_NEARD_BUILD_DIR)
$(COVERAGE_DBUS_NEARD_OBJS): | $(COVERAGE_DBUS_NEARD_BUILD_DIR)

$(DBUS_NEARD_GEN_DIR):
	mkdir -p $@

$(DEBUG_DBUS_NEARD_BUILD_DIR):
	mkdir -p $@

$(RELEASE_DBUS_NEARD_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_DBUS_NEARD_BUILD_DIR):
	mkdir -p $@

#
# Setting plugin
#

SETTINGS_DIR = settings
SETTINGS_PLUGIN_SRC = \
  settings_plugin.c

SETTINGS_GEN_SRC = \
  org.sailfishos.nfc.Settings.c

SETTINGS_SRC = \
  $(SETTINGS_GEN_SRC) \
  $(SETTINGS_PLUGIN_SRC)

SETTINGS_GEN_DIR = $(GEN_DIR)/$(SETTINGS_DIR)
DEBUG_SETTINGS_BUILD_DIR = $(DEBUG_BUILD_DIR)/$(SETTINGS_DIR)
RELEASE_SETTINGS_BUILD_DIR = $(RELEASE_BUILD_DIR)/$(SETTINGS_DIR)
COVERAGE_SETTINGS_BUILD_DIR = $(COVERAGE_BUILD_DIR)/$(SETTINGS_DIR)

DEBUG_SETTINGS_OBJS = $(SETTINGS_SRC:%.c=$(DEBUG_SETTINGS_BUILD_DIR)/%.o)
RELEASE_SETTINGS_OBJS = $(SETTINGS_SRC:%.c=$(RELEASE_SETTINGS_BUILD_DIR)/%.o)
COVERAGE_SETTINGS_OBJS = $(SETTINGS_SRC:%.c=$(COVERAGE_SETTINGS_BUILD_DIR)/%.o)

DEBUG_OBJS += $(DEBUG_SETTINGS_OBJS)
RELEASE_OBJS += $(RELEASE_SETTINGS_OBJS)
COVERAGE_OBJS += $(COVERAGE_SETTINGS_OBJS)

$(SETTINGS_GEN_SRC:%=$(SETTINGS_GEN_DIR)/%): | $(SETTINGS_GEN_DIR)
$(SETTINGS_PLUGIN_SRC:%=$(SETTINGS_DIR)/%): | $(SETTINGS_GEN_SRC:%=$(SETTINGS_GEN_DIR)/%)
$(DEBUG_SETTINGS_OBJS): | $(DEBUG_SETTINGS_BUILD_DIR)
$(RELEASE_SETTINGS_OBJS): | $(RELEASE_SETTINGS_BUILD_DIR)
$(COVERAGE_SETTINGS_OBJS): | $(COVERAGE_SETTINGS_BUILD_DIR)

$(SETTINGS_GEN_DIR):
	mkdir -p $@

$(DEBUG_SETTINGS_BUILD_DIR):
	mkdir -p $@

$(RELEASE_SETTINGS_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_SETTINGS_BUILD_DIR):
	mkdir -p $@

#
# D-Bus handlers plugin
#

DBUS_HANDLERS_DIR = dbus_handlers
DBUS_HANDLERS_SRC = \
  dbus_handlers_adapter.c \
  dbus_handlers_config.c \
  dbus_handlers_plugin.c \
  dbus_handlers_tag.c \
  dbus_handlers_type_generic.c \
  dbus_handlers_type_mediatype.c \
  dbus_handlers_type_sp.c \
  dbus_handlers_type_text.c \
  dbus_handlers_type_uri.c \
  dbus_handlers.c

DEBUG_DBUS_HANDLERS_BUILD_DIR = $(DEBUG_BUILD_DIR)/$(DBUS_HANDLERS_DIR)
RELEASE_DBUS_HANDLERS_BUILD_DIR = $(RELEASE_BUILD_DIR)/$(DBUS_HANDLERS_DIR)
COVERAGE_DBUS_HANDLERS_BUILD_DIR = $(COVERAGE_BUILD_DIR)/$(DBUS_HANDLERS_DIR)

DEBUG_DBUS_HANDLERS_OBJS = $(DBUS_HANDLERS_SRC:%.c=$(DEBUG_DBUS_HANDLERS_BUILD_DIR)/%.o)
RELEASE_DBUS_HANDLERS_OBJS = $(DBUS_HANDLERS_SRC:%.c=$(RELEASE_DBUS_HANDLERS_BUILD_DIR)/%.o)
COVERAGE_DBUS_HANDLERS_OBJS = $(DBUS_HANDLERS_SRC:%.c=$(COVERAGE_DBUS_HANDLERS_BUILD_DIR)/%.o)

DEBUG_OBJS += $(DEBUG_DBUS_HANDLERS_OBJS)
RELEASE_OBJS += $(RELEASE_DBUS_HANDLERS_OBJS)
COVERAGE_OBJS += $(COVERAGE_DBUS_HANDLERS_OBJS)

$(DEBUG_DBUS_HANDLERS_OBJS): | $(DEBUG_DBUS_HANDLERS_BUILD_DIR)
$(RELEASE_DBUS_HANDLERS_OBJS): | $(RELEASE_DBUS_HANDLERS_BUILD_DIR)
$(COVERAGE_DBUS_HANDLERS_OBJS): | $(COVERAGE_DBUS_HANDLERS_BUILD_DIR)

$(DEBUG_DBUS_HANDLERS_BUILD_DIR):
	mkdir -p $@

$(RELEASE_DBUS_HANDLERS_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_DBUS_HANDLERS_BUILD_DIR):
	mkdir -p $@

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wstrict-aliasing -Wunused-result
INCLUDES = -I. -I../core/include -I$(GEN_DIR)
FULL_CFLAGS = -fPIC $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
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

coverage: $(COVERAGE_LIB)

debug_lib: $(DEBUG_LIB)

release_lib: $(RELEASE_LIB)

coverage_lib: $(COVERAGE_LIB)

print_debug_lib:
	@echo $(DEBUG_LIB)

print_release_lib:
	@echo $(RELEASE_LIB)

print_coverage_lib:
	@echo $(COVERAGE_LIB)

clean:
	rm -f *~ */*~
	rm -fr $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_BUILD_DIR):
	mkdir -p $@

$(GEN_DIR)/%.c: %.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o: %.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: %.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/%.o: %.c
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

#
# Install
#

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d

INSTALL_DBUS_CONF_DIR = $(DESTDIR)/etc/dbus-1/system.d

install: $(INSTALL_DBUS_CONF_DIR)
	$(INSTALL) -m 644 */*.conf $(INSTALL_DBUS_CONF_DIR)

$(INSTALL_DBUS_CONF_DIR):
	$(INSTALL_DIRS) $@
