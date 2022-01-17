# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release coverage
.PHONY: debug_core_lib release_core_lib coverage_core_lib

#
# Real test makefile defines EXE (and possibly SRC) and includes this one.
#

ifndef EXE
${error EXE not defined}
endif

SRC ?= $(EXE).c
COMMON_SRC ?= test_main.c

#
# Default target
#

all: debug release

#
# Directories
#

SRC_DIR = .
CORE_DIR = ../../core
COMMON_DIR = ../common
BUILD_DIR = build
GEN_DIR = $(BUILD_DIR)
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release
COVERAGE_BUILD_DIR = $(BUILD_DIR)/coverage

#
# Required packages
#

PKGS += libglibutil glib-2.0 gobject-2.0 gio-2.0 gio-unix-2.0

ifneq ($(GEN_SRC),)
# Additional requirements for generated stubs
PKGS += gio-unix-2.0
INCLUDES += -I. -I$(GEN_DIR)

# Make sure stubs get generated before compilation starts
$(GEN_SRC:%=$(GEN_DIR)/%): | $(GEN_DIR)
$(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o): | $(GEN_SRC:%=$(GEN_DIR)/%)
$(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o): | $(GEN_SRC:%=$(GEN_DIR)/%)
$(SRC:%.c=$(COVERAGE_BUILD_DIR)/%.o): | $(GEN_SRC:%=$(GEN_DIR)/%)
endif

ifeq ($(SPEC_DIR),)
SPEC_DIR = .
endif

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS += -Wall
INCLUDES += -I$(COMMON_DIR) -I$(CORE_DIR)/src -I$(CORE_DIR)/include
BASE_FLAGS = -fPIC
BASE_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS)
BASE_CFLAGS = $(BASE_FLAGS) $(CFLAGS)
FULL_CFLAGS = $(BASE_CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS) $(CFLAGS_PKGS))
FULL_LDFLAGS = $(BASE_LDFLAGS)
LIBS = $(shell pkg-config --libs $(PKGS)) -ldl
DEBUG_FLAGS = -g
RELEASE_FLAGS =
COVERAGE_FLAGS = -g

DEBUG_LDFLAGS = $(FULL_LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS) $(RELEASE_FLAGS)
COVERAGE_LDFLAGS = $(FULL_LDFLAGS) $(COVERAGE_FLAGS) --coverage

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
COVERAGE_CFLAGS = $(FULL_CFLAGS) $(COVERAGE_FLAGS) --coverage

CORE_LIB = libnfc-core.a
CORE_BUILD_DIR = $(CORE_DIR)/build
DEBUG_CORE_LIB = $(CORE_BUILD_DIR)/debug/$(CORE_LIB)
RELEASE_CORE_LIB = $(CORE_BUILD_DIR)/release/$(CORE_LIB)
COVERAGE_CORE_LIB = $(CORE_BUILD_DIR)/coverage/$(CORE_LIB)

DEBUG_LIBS += $(DEBUG_CORE_LIB) $(LIBS)
RELEASE_LIBS += $(RELEASE_CORE_LIB) $(LIBS)
COVERAGE_LIBS += $(COVERAGE_CORE_LIB) $(LIBS)

#
# Files
#

DEBUG_OBJS = \
  $(COMMON_SRC:%.c=$(DEBUG_BUILD_DIR)/common_%.o) \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/gen_%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(COMMON_SRC:%.c=$(RELEASE_BUILD_DIR)/common_%.o) \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/gen_%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
COVERAGE_OBJS = \
  $(COMMON_SRC:%.c=$(COVERAGE_BUILD_DIR)/common_%.o) \
  $(GEN_SRC:%.c=$(COVERAGE_BUILD_DIR)/gen_%.o) \
  $(SRC:%.c=$(COVERAGE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS += $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_CORE_LIB): | debug_core_lib
$(RELEASE_CORE_LIB): | release_core_lib
$(COVERAGE_CORE_LIB): | coverage_core_lib

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)
$(COVERAGE_OBJS): | $(COVERAGE_BUILD_DIR)

DEBUG_DEPS += $(DEBUG_CORE_LIB)
RELEASE_DEPS += $(RELEASE_CORE_LIB)
COVERAGE_DEPS += $(COVERAGE_CORE_LIB)

#
# Rules
#

DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)
COVERAGE_EXE = $(COVERAGE_BUILD_DIR)/$(EXE)

debug: debug_core_lib $(DEBUG_EXE)

release: release_core_lib $(RELEASE_EXE)

coverage: coverage_core_lib $(COVERAGE_EXE)

unitclean:
	rm -f *~
	rm -fr $(BUILD_DIR)

clean: unitclean

cleaner: unitclean
	@$(MAKE) -C "$(CORE_DIR)" clean
ifneq ($(PLUGINS_DIR),)
	@$(MAKE) -C "$(PLUGINS_DIR)" clean
endif

test_banner:
	@echo "===========" $(EXE) "=========== "

test: test_banner debug 
	@$(DEBUG_EXE)

valgrind: test_banner debug
	@G_DEBUG=gc-friendly G_SLICE=always-malloc valgrind --tool=memcheck --leak-check=full --show-possibly-lost=no $(DEBUG_EXE)

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_BUILD_DIR):
	mkdir -p $@

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/common_%.o : $(COMMON_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/common_%.o : $(COMMON_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/common_%.o : $(COMMON_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/gen_%.o : $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/gen_%.o : $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/gen_%.o : $(GEN_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_DEPS) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(EXTRA_EXE_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_DEPS) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(EXTRA_EXE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@

$(COVERAGE_EXE): $(COVERAGE_DEPS) $(COVERAGE_OBJS)
	$(LD) $(COVERAGE_LDFLAGS) $(EXTRA_EXE_LDFLAGS) $(COVERAGE_OBJS) $(COVERAGE_LIBS) -o $@

debug_core_lib:
	$(MAKE) -C $(CORE_DIR) debug_lib

release_core_lib:
	$(MAKE) -C $(CORE_DIR) release_lib

coverage_core_lib:
	$(MAKE) -C $(CORE_DIR) coverage_lib
