# Copyright 2014 Peter Goodman, all rights reserved.

GRANARY_LD ?= gold
GRANARY_CC ?= clang
GRANARY_CXX ?= clang++
GRANARY_LLVM_LINK ?= llvm-link-3.5
GRANARY_AR ?= ar
GRANARY_PYTHON ?= python2.7

GRANARY_ARCH ?= x86-64
GRANARY_OS ?= linux
GRANARY_TARGET ?= debug# release, debug, test
GRANARY_WHERE ?= user# user, kernel
GRANARY_DRIVER ?= xed2-intel64
GRANARY_STANDALONE ?= 0# 0, 1
GRANARY_NAME ?= granary
GRANARY_WITH_VALGRIND ?= 0# 0, 1
GRANARY_EXPORT_HEADERS_DIR ?= /usr/local/include/granary
GRANARY_EXPORT_LIB_DIR ?= /usr/local/lib
GRANARY_LINT_CODE ?= 1# 0, 1
GRANARY_HEADER_MACRO_DEFS =

# Force Granary into stand-alone mode in user space if we're making a test
# build.
ifeq (test,$(GRANARY_TARGET))
	GRANARY_WHERE := user
endif

GRANARY_KERNEL_VERSION = $(shell uname -r)
GRANARY_DEFAULT_KERNEL_DIR = /lib/modules/$(GRANARY_KERNEL_VERSION)/build
GRANARY_KERNEL_DIR ?= $(GRANARY_DEFAULT_KERNEL_DIR)

GRANARY_SRC_DIR ?= $(shell pwd)
GRANARY_CLIENT_DIR ?= $(GRANARY_SRC_DIR)/examples

# Make the binary directory target-specific.
GRANARY_BIN_DIR ?= $(GRANARY_SRC_DIR)/bin
GRANARY_BIN_DIR := $(GRANARY_BIN_DIR)/$(GRANARY_TARGET)_$(GRANARY_WHERE)

GRANARY_WHERE_SRC_DIR = $(GRANARY_SRC_DIR)/granary/$(GRANARY_WHERE)
GRANARY_WHERE_BIN_DIR = $(GRANARY_BIN_DIR)/granary/$(GRANARY_WHERE)

GRANARY_ARCH_SRC_DIR ?=  $(GRANARY_SRC_DIR)/granary/arch/$(GRANARY_ARCH)
GRANARY_ARCH_BIN_DIR := $(GRANARY_BIN_DIR)/granary/arch/$(GRANARY_ARCH)

# Google Test and Google Mock setup
GRANARY_TEST_SRC_DIR := $(GRANARY_SRC_DIR)/test
GRANARY_TEST_BIN_DIR := $(GRANARY_BIN_DIR)/test
GRANARY_GTEST_SRC_DIR := $(GRANARY_SRC_DIR)/dependencies/googletest
GRANARY_GTEST_BIN_DIR := $(GRANARY_BIN_DIR)/dependencies/googletest
GRANARY_GMOCK_SRC_DIR := $(GRANARY_SRC_DIR)/dependencies/googlemock/trunk
GRANARY_GMOCK_BIN_DIR := $(GRANARY_BIN_DIR)/dependencies/googlemock

# Convenient way to totally ignore output of a shell command that might fail.
GRANARY_DEV_NULL = > /dev/null 2>&1 ||:

# Figure out the generated files directories.
# TODO(pag): Make generated source files target-specific?
GRANARY_GEN_SRC_DIR = $(GRANARY_SRC_DIR)/generated
GRANARY_GEN_BIN_DIR = $(GRANARY_BIN_DIR)/generated

GRANARY_EXTRA_CXX_FLAGS ?=
GRANARY_EXTRA_CC_FLAGS ?=

GRANARY_LD_FLAGS =
GRANARY_LD_FLAGS_EARLY =
GRANARY_LD_FLAGS_LATE =
GRANARY_CC_FLAGS = $(GRANARY_EXTRA_CC_FLAGS)
GRANARY_CXX_FLAGS = $(GRANARY_EXTRA_CXX_FLAGS)
GRANARY_ASM_FLAGS = -DGRANARY_ASSEMBLY
GRANARY_COMMON_FLAGS =
GRANARY_COMMON_ARCH_FLAGS =
GRANARY_COMMON_WARN_FLAGS =
GRANARY_DEBUG_FLAGS =

ifdef GRANARY_CLIENT_NAME
	GRANARY_CLIENT_BIN_DIR ?= $(GRANARY_BIN_DIR)/clients
	GRANARY_STANDALONE = 0
	GRANARY_LD_FLAGS_EARLY += -shared
endif

# Include path. All source files should use relative imports, with respect to
# `GRANARY_SRC_DIR`.
GRANARY_COMMON_FLAGS += -I$(GRANARY_SRC_DIR) -DGRANARY_NAME=$(GRANARY_NAME)
GRANARY_COMMON_FLAGS += -DGRANARY_WHERE_$(GRANARY_WHERE)
GRANARY_HEADER_MACRO_DEFS += -DGRANARY_WHERE_$(GRANARY_WHERE)

# Use GNU C99 and standard C++11.
GRANARY_CC_FLAGS += -std=gnu99
GRANARY_COMMON_CXX_FLAGS = -std=c++11 -stdlib=libc++

# Enable valgrind checking of code.
ifeq (1,$(GRANARY_WITH_VALGRIND))
	GRANARY_CXX_FLAGS += -DGRANARY_WITH_VALGRIND
	GRANARY_HEADER_MACRO_DEFS += -DGRANARY_WITH_VALGRIND
endif
ifeq (test,$(GRANARY_TARGET))
	GRANARY_STANDALONE := 1
	GRANARY_COMMON_FLAGS += -DGRANARY_TEST
	GRANARY_HEADER_MACRO_DEFS += -DGRANARY_TEST
endif
ifeq (1,$(GRANARY_STANDALONE))
	GRANARY_COMMON_FLAGS += -DGRANARY_STANDALONE
	GRANARY_HEADER_MACRO_DEFS += -DGRANARY_STANDALONE
endif

# Get the opposite of the where-directory.
ifeq (kernel,$(GRANARY_WHERE))
	GRANARY_NOT_WHERE = user
else
	GRANARY_NOT_WHERE = kernel
	GRANARY_COMMON_ARCH_FLAGS += -ftls-model=initial-exec
endif

# Enable/disable features based on whether or not this is a debug or release
# build.
ifeq (release,$(GRANARY_TARGET))  # release
	GRANARY_DEBUG_FLAGS += -g3
    GRANARY_COMMON_FLAGS += -O3 $(GRANARY_DEBUG_FLAGS)
    GRANARY_LD_FLAGS += -O3 $(GRANARY_DEBUG_FLAGS)
else  # debug, test
	GRANARY_DEBUG_FLAGS += -g3
	GRANARY_COMMON_FLAGS += -O0 $(GRANARY_DEBUG_FLAGS) -DGRANARY_DEBUG
	GRANARY_HEADER_MACRO_DEFS += -DGRANARY_DEBUG
	GRANARY_LD_FLAGS += $(GRANARY_DEBUG_FLAGS)
endif

# Make sure certain architectural features are disabled.
GRANARY_COMMON_ARCH_FLAGS += -ffreestanding -fno-common -fno-builtin
GRANARY_COMMON_ARCH_FLAGS += -mno-red-zone -m64 -mtune=generic -msoft-float
GRANARY_COMMON_ARCH_FLAGS += -fno-stack-protector -fno-math-errno
GRANARY_COMMON_ARCH_FLAGS += -minline-all-stringops
GRANARY_COMMON_ARCH_FLAGS += -fno-assume-sane-operator-new

ifeq ("x86-64","$(GRANARY_ARCH)")
	GRANARY_COMMON_ARCH_FLAGS += -mno-mmx -mno-sse -mno-sse2 -mno-sse3
	GRANARY_COMMON_ARCH_FLAGS += -mno-sse4.1 -mno-sse4.2 -mno-sse4 -mno-sse4a
	GRANARY_COMMON_ARCH_FLAGS += -mno-avx -mno-3dnow
endif
ifeq (kernel,$(GRANARY_WHERE))
	GRANARY_COMMON_ARCH_FLAGS += -nostdlib
endif

# Make sure certain language features are disabled.
GRANARY_COMMON_CXX_FLAGS += -fno-asynchronous-unwind-tables
GRANARY_COMMON_CXX_FLAGS += -fno-rtti -fno-exceptions 

ifeq (kernel,$(GRANARY_WHERE))  # Make sure we follow the kernel ABI.
	GRANARY_COMMON_ARCH_FLAGS += -mkernel -mcmodel=kernel
endif

# Make sure that we generate position-independent code, regardless of whether
# or not we're in user space or kernel space.
GRANARY_COMMON_FLAGS += -fPIC

# Be crazy about warnings with debug (but not release/test) builds. We also only
# do this when we *aren't* debugging with Valgrind because when we debug with
# valgrind, we mostly depend on `malloc` for allocation, which doesn't respect
# the proper alignment requirements of many structures.
ifeq (debug0,"$(GRANARY_TARGET)$(GRANARY_WITH_VALGRIND)")
	GRANARY_COMMON_ARCH_FLAGS += -ftrapv -fsanitize=undefined-trap
	GRANARY_COMMON_ARCH_FLAGS += -fsanitize-undefined-trap-on-error

    GRANARY_COMMON_WARN_FLAGS += -Weverything -Wno-c++98-compat-pedantic
    GRANARY_COMMON_WARN_FLAGS += -Wno-padded -Wno-missing-prototypes
    GRANARY_COMMON_WARN_FLAGS += -Wno-missing-variable-declarations
    GRANARY_COMMON_WARN_FLAGS += -Wno-switch-enum
endif

# Enable various warnings and errors.
GRANARY_COMMON_WARN_FLAGS += -Wall -Wextra -Werror -pedantic -pedantic-errors
GRANARY_COMMON_WARN_FLAGS += -Wcast-qual -Wshadow -Wpointer-arith
GRANARY_COMMON_WARN_FLAGS += -Wconversion -Wuninitialized -Winit-self 
GRANARY_COMMON_WARN_FLAGS += -Wmissing-declarations -Wwrite-strings -Wfloat-equal
GRANARY_COMMON_WARN_FLAGS += -fstrict-aliasing -Wstrict-aliasing=2 -Wstrict-overflow=2
GRANARY_COMMON_WARN_FLAGS += -Wcast-align -Wredundant-decls -Wvolatile-register-var
GRANARY_COMMON_WARN_FLAGS += -Wstack-protector -Wold-style-cast
GRANARY_COMMON_WARN_FLAGS += -Wframe-larger-than=4096

# Disable various warnings and errors.
GRANARY_COMMON_WARN_FLAGS += -Wno-nested-anon-types -Wno-variadic-macros
GRANARY_COMMON_WARN_FLAGS += -Wno-extended-offsetof -Wno-gnu -Wno-long-long
GRANARY_COMMON_WARN_FLAGS += -Wno-c11-extensions

# Add the common flags to both the C++ and C compiler flags.
GRANARY_CXX_FLAGS += $(GRANARY_COMMON_CXX_FLAGS) $(GRANARY_COMMON_FLAGS)
GRANARY_CXX_FLAGS += $(GRANARY_COMMON_WARN_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_CC_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_ASM_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_LD_FLAGS += -fPIC $(GRANARY_COMMON_ARCH_FLAGS)

ifeq (0,$(GRANARY_STANDALONE))
	GRANARY_LD_FLAGS_LATE += "-Wl,-flat_namespace"
endif

# Mask some of the libc and libc++ symbols with Granary equivalents.
GRANARY_LD_FLAGS_LATE += "-Wl,--version-script=$(GRANARY_SRC_DIR)/symver.map"

# Code linting.
GRANARY_LINT_DIR = $(GRANARY_SRC_DIR)/dependencies/cpplint
GRANARY_LINT_FLAGS = --verbose=5
GRANARY_LINT = $(GRANARY_PYTHON) $(GRANARY_LINT_DIR)/cpplint.py $(GRANARY_LINT_FLAGS)

# Specific files that are used by user space, kernel space, and test cases.
GRANARY_MERGED_OBJ = $(GRANARY_BIN_DIR)/granary_merged.o

.SILENT :
