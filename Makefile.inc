# Copyright 2014 Peter Goodman, all rights reserved.

GRANARY_CC ?= clang
GRANARY_CC_STD ?= gnu99
GRANARY_CXX ?= clang++
GRANARY_CXX_LIB ?= libc++
GRANARY_CXX_STD ?= c++11
GRANARY_LD ?= gold
GRANARY_LLVM_LINK ?= llvm-link-3.5
GRANARY_AR ?= ar
GRANARY_PYTHON ?= python2.7
GRANARY_SCAN_BUILD ?= 0

GRANARY_ARCH ?= x86-64
GRANARY_OS ?= linux
GRANARY_TARGET ?= debug# release, debug, test
GRANARY_WHERE ?= user# user, kernel
GRANARY_TRIPLE := $(GRANARY_TARGET)_$(GRANARY_OS)_$(GRANARY_WHERE)
GRANARY_DRIVER ?= xed2-intel64
GRANARY_NAME ?= granary
GRANARY_WITH_VALGRIND ?= 0# 0, 1
GRANARY_LINT_CODE ?= 1# 0, 1
GRANARY_HEADER_MACRO_DEFS =
GRANARY_RECURSIVE ?= 0

# Allow the clang static analyzer to interpose on the build.
#
# Note: To get this to work, I had to manually modify the file
#       `/usr/share/clang/scan-build/ccc-analyzer` and set the default
#       compilers to `clang` and `clang++` in all cases. Alternatively, the
#       following should achieve the same results:
#           export CCC_CC=clang
#           export CCC_CXX=clang++
ifeq (1,$(GRANARY_SCAN_BUILD))
	GRANARY_OLD_CC := $(GRANARY_CC)
	GRANARY_OLD_CXX := $(GRANARY_CXX)
	GRANARY_SCAN_CMD := scan-build --use-cc=$(GRANARY_OLD_CC)
	GRANARY_SCAN_CMD += --use-c++=$(GRANARY_OLD_CXX)
	GRANARY_CC :=  $(GRANARY_SCAN_CMD) $(GRANARY_OLD_CC)
	GRANARY_CXX := $(GRANARY_SCAN_CMD) $(GRANARY_OLD_CXX)
endif

# Force Granary into stand-alone mode in user space if we're making a test
# build.
ifeq (test,$(GRANARY_TARGET))
	GRANARY_WHERE := user
endif

GRANARY_DEFAULT_KERNEL_VERSION = $(shell uname -r)
GRANARY_DEFAULT_KERNEL_DIR = /lib/modules/$(GRANARY_DEFAULT_KERNEL_VERSION)/build
GRANARY_KERNEL_DIR ?= $(GRANARY_SRC_DIR)/dependencies/linux

GRANARY_SRC_DIR ?= $(shell pwd)

# Find all of Granary's clients.
GRANARY_CLIENT_DIR ?= $(GRANARY_SRC_DIR)/clients
GRANARY_CLIENTS ?= $(notdir $(shell find $(GRANARY_CLIENT_DIR) -mindepth 1 -maxdepth 1 -type d))

# Make the binary directory target-specific.
GRANARY_BIN_DIR ?= $(GRANARY_SRC_DIR)/bin
GRANARY_BIN_DIR := $(GRANARY_BIN_DIR)/$(GRANARY_TRIPLE)

GRANARY_WHERE_SRC_DIR = $(GRANARY_SRC_DIR)/os/$(GRANARY_OS)/$(GRANARY_WHERE)
GRANARY_WHERE_BIN_DIR = $(GRANARY_BIN_DIR)/os/$(GRANARY_OS)/$(GRANARY_WHERE)

GRANARY_OS_ARCH_SRC_DIR = $(GRANARY_SRC_DIR)/os/$(GRANARY_OS)/arch/$(GRANARY_ARCH)
GRANARY_OS_ARCH_BIN_DIR = $(GRANARY_BIN_DIR)/os/$(GRANARY_OS)/arch/$(GRANARY_ARCH)

GRANARY_ARCH_SRC_DIR ?=  $(GRANARY_SRC_DIR)/arch/$(GRANARY_ARCH)
GRANARY_ARCH_BIN_DIR := $(GRANARY_BIN_DIR)/arch/$(GRANARY_ARCH)

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
GRANARY_GEN_SRC_DIR = $(GRANARY_SRC_DIR)/generated
GRANARY_GEN_BIN_DIR = $(GRANARY_BIN_DIR)/generated

GRANARY_EXTRA_CXX_FLAGS ?=
GRANARY_EXTRA_CC_FLAGS ?=

GRANARY_LD_FLAGS =
GRANARY_LD_FLAGS_EARLY =
GRANARY_LD_FLAGS_LATE =
GRANARY_CC_FLAGS = $(GRANARY_EXTRA_CC_FLAGS) -isystem $(GRANARY_HEADERS_DIR)
GRANARY_CXX_FLAGS = $(GRANARY_EXTRA_CXX_FLAGS) -isystem $(GRANARY_HEADERS_DIR)
GRANARY_ASM_FLAGS = -DGRANARY_ASSEMBLY
GRANARY_COMMON_FLAGS =
GRANARY_COMMON_ARCH_FLAGS =
GRANARY_COMMON_WARN_FLAGS =
GRANARY_DEBUG_FLAGS =
GRANARY_HEADERS_DIR = $(GRANARY_SRC_DIR)/generated/$(GRANARY_TRIPLE)
GRANARY_HEADERS = $(GRANARY_HEADERS_DIR)/granary.h

ifdef GRANARY_CLIENT_NAME
	GRANARY_CLIENT_BIN_DIR ?= $(GRANARY_BIN_DIR)/clients
	GRANARY_LD_FLAGS_EARLY += -shared
endif

# Include path. All source files should use relative imports, with respect to
# `GRANARY_SRC_DIR`.
GRANARY_COMMON_FLAGS += -I$(GRANARY_SRC_DIR) -DGRANARY_NAME=$(GRANARY_NAME)
GRANARY_COMMON_FLAGS += 
GRANARY_COMMON_FLAGS += 

GRANARY_COMMON_MACROS =  -DGRANARY_TRIPLE_$(GRANARY_TRIPLE)
GRANARY_COMMON_MACROS += -DGRANARY_WHERE_$(GRANARY_WHERE)
GRANARY_COMMON_MACROS += -DGRANARY_TARGET_$(GRANARY_TARGET)
GRANARY_COMMON_MACROS += -DGRANARY_OS_$(GRANARY_OS)

# Use GNU C99 and standard C++11.
GRANARY_CC_FLAGS += -std=$(GRANARY_CC_STD)
GRANARY_COMMON_CXX_FLAGS = -std=$(GRANARY_CXX_STD) -stdlib=$(GRANARY_CXX_LIB)

# Enable valgrind checking of code.
ifeq (1,$(GRANARY_WITH_VALGRIND))
	GRANARY_COMMON_MACROS += -DGRANARY_WITH_VALGRIND
	GRANARY_CXX_FLAGS += -fno-inline
endif

# Enable recursive instrumentation (Granary instrumenting Granary).
ifeq (1,$(GRANARY_RECURSIVE))
  GRANARY_COMMON_MACROS += -DGRANARY_RECURSIVE
endif

GRANARY_HEADER_MACRO_DEFS += $(GRANARY_COMMON_MACROS)
GRANARY_COMMON_FLAGS += $(GRANARY_COMMON_MACROS)

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
	GRANARY_COMMON_FLAGS += -O0 $(GRANARY_DEBUG_FLAGS)
	GRANARY_LD_FLAGS += $(GRANARY_DEBUG_FLAGS)
	GRANARY_LD_FLAGS_LATE += "-Wl,-g"
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
	GRANARY_LD_FLAGS_EARLY += -nodefaultlibs
	GRANARY_COMMON_ARCH_FLAGS += -mkernel -mcmodel=kernel
endif

# Make sure certain language features are disabled.
GRANARY_COMMON_CXX_FLAGS += -fno-asynchronous-unwind-tables
GRANARY_COMMON_CXX_FLAGS += -fno-rtti -fno-exceptions

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
GRANARY_COMMON_WARN_FLAGS += -Wconversion -Winit-self -Wfloat-equal
GRANARY_COMMON_WARN_FLAGS += -Wuninitialized -Wsometimes-uninitialized
GRANARY_COMMON_WARN_FLAGS += -Wmissing-declarations -Wwrite-strings
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
GRANARY_ASM_FLAGS += $(GRANARY_COMMON_FLAGS)
GRANARY_LD_FLAGS += -fPIC $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_LD_FLAGS_LATE += "-Wl,-flat_namespace"

# Mask some of the libc and libc++ symbols with Granary equivalents.
GRANARY_LD_FLAGS_LATE += "-Wl,--version-script=$(GRANARY_SRC_DIR)/symbol.versions"
ifeq (release,$(GRANARY_TARGET))
	GRANARY_LD_FLAGS_LATE += "-Wl,--retain-symbols-file=$(GRANARY_SRC_DIR)/symbol.exports"
endif

# Code linting.
GRANARY_LINT_DIR = $(GRANARY_SRC_DIR)/dependencies/cpplint
GRANARY_LINT_FLAGS = --verbose=5
GRANARY_LINT = $(GRANARY_PYTHON) $(GRANARY_LINT_DIR)/cpplint.py $(GRANARY_LINT_FLAGS)

# Specific files that are used by user space, kernel space, and test cases.
GRANARY_MERGED_OBJ = $(GRANARY_BIN_DIR)/granary.o
GRANARY_DRIVER_OBJ = $(GRANARY_BIN_DIR)/driver.o
GRANARY_ARCH_OBJ = $(GRANARY_BIN_DIR)/arch.o
GRANARY_OS_OBJ = $(GRANARY_BIN_DIR)/os.o
GRANARY_CLIENT_OBJS = $(addsuffix _client.o,$(addprefix $(GRANARY_BIN_DIR)/,$(GRANARY_CLIENTS)))

# Objects that must be combined into the final Granary executable.
GRANARY_OBJS = $(GRANARY_MERGED_OBJ) $(GRANARY_DRIVER_OBJ)
GRANARY_OBJS += $(GRANARY_OS_OBJ) $(GRANARY_CLIENT_OBJS)

GRANARY_LINT_TARGET =

# Should Granary's main code be linted?
ifeq (1,$(GRANARY_LINT_CODE))
	GRANARY_LINT_TARGET = lint
endif

# Common generic rules that can apply to all Granary code.
ifndef GRANARY_IN_TEST_DIR
# Run the C pre-processor on the *.asm files, then run those through the
# ASM post-processor to do extra token pasting and macro expansion.
$(GRANARY_BIN_DIR)/%.S: $(GRANARY_SRC_DIR)/%.asm
	@echo "Processing assembly file $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -E -o $@ -x c -std=c99 $< 
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/post_process_asm.py $@

# Compile an assembly source file into an object file.
$(GRANARY_BIN_DIR)/%.o :: $(GRANARY_BIN_DIR)/%.S
	@echo "Building ASM object $@"
	$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -c $< -o $@

# Compile C++ files to object files.
$(GRANARY_BIN_DIR)/%.o :: $(GRANARY_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -c $< -o $@

# Compile C files to object files.
$(GRANARY_BIN_DIR)/%.o :: $(GRANARY_SRC_DIR)/%.c
	@echo "Building C object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -c $< -o $@

# Compile C++ files to LLVM bitcode.
#
# Note: This enables link-time optimization.
$(GRANARY_BIN_DIR)/%.bc :: $(GRANARY_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -flto -c $< -o $@

# Compile a LLVM bitcode file to an object file.
$(GRANARY_BIN_DIR)/%.o :: $(GRANARY_BIN_DIR)/%.bc
	@echo "Building BC object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) -c $< -o $@
endif

.SILENT :
