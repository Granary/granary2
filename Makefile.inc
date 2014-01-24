# Copyright 2014 Peter Goodman, all rights reserved.

GRANARY_LD ?= ld
GRANARY_CC ?= clang
GRANARY_CXX ?= clang++
GRANARY_PYTHON ?= python2.7

GRANARY_ARCH ?= x86-64
GRANARY_OS ?= linux
GRANARY_TARGET ?= debug
GRANARY_WHERE ?= user
GRANARY_DRIVER ?= dynamorio
GRANARY_STANDALONE ?= 1
GRANARY_NAME ?= granary
GRANARY_WITH_VALGRIND ?= 0

GRANARY_SRC_DIR ?= /home/pag/Code/Granary+
GRANARY_DRIVER_DIR ?= $(GRANARY_SRC_DIR)/granary/driver/$(GRANARY_DRIVER)
GRANARY_WHERE_DIR ?= $(GRANARY_SRC_DIR)/granary/$(GRANARY_WHERE)
GRANARY_ARCH_DIR ?=  $(GRANARY_SRC_DIR)/granary/arch/$(GRANARY_ARCH)

# Make the binary directory target-specific.
GRANARY_BIN_DIR ?= /home/pag/Code/Granary+/bin
GRANARY_BIN_DIR := $(GRANARY_BIN_DIR)/$(GRANARY_TARGET)

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
GRANARY_ASM_FLAGS =
GRANARY_COMMON_FLAGS =
GRANARY_COMMON_ARCH_FLAGS =
GRANARY_COMMON_WARN_FLAGS =

# Include path. All source files should use relative imports, with respect to
# `GRANARY_SRC_DIR`.
GRANARY_COMMON_FLAGS += -I$(GRANARY_SRC_DIR)
GRANARY_COMMON_FLAGS += -DGRANARY_STANDALONE=$(GRANARY_STANDALONE)

# Use C11 and C++11.
GRANARY_CC_FLAGS += -std=gnu99
GRANARY_CXX_FLAGS += -std=c++11

ifeq ($(GRANARY_WHERE),kernel)
	GRANARY_STANDALONE := 0
endif

# Enable valgrind checking of code.
ifeq ($(GRANARY_WITH_VALGRIND),1)
	GRANARY_CXX_FLAGS += -DGRANARY_WITH_VALGRIND
endif

# Enable/disable features based on whether or not this is a debug or release
# build.
ifeq ($(GRANARY_TARGET),debug)
    GRANARY_COMMON_FLAGS += -O0 -g3 -DGRANARY_DEBUG
    GRANARY_LD_FLAGS += -O0 -g3
else
    GRANARY_COMMON_FLAGS += -O3 -g0
    GRANARY_LD_FLAGS += -O3 -g0
endif

# Make sure certain architectural features are disabled.
GRANARY_COMMON_ARCH_FLAGS += -ffreestanding -fno-common -fno-builtin
GRANARY_COMMON_ARCH_FLAGS += -mno-red-zone -m64 -march=native
GRANARY_COMMON_ARCH_FLAGS += -fno-stack-protector -fno-math-errno
GRANARY_COMMON_ARCH_FLAGS += -fvisibility=hidden
ifeq ("x86-64","$(GRANARY_ARCH)")
	GRANARY_COMMON_ARCH_FLAGS += -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
endif
ifneq (user,$(GRANARY_WHERE))
	GRANARY_COMMON_ARCH_FLAGS += -nostdlib
endif

# Make sure certain language features are disabled.
GRANARY_CXX_FLAGS += -fno-asynchronous-unwind-tables -fno-rtti -fno-exceptions 

ifeq ($(GRANARY_WHERE),kernel)  # Make sure we follow the kernel ABI.
	GRANARY_COMMON_ARCH_FLAGS += -mkernel -mcmodel=kernel
endif

# Make sure that we generate position-independent code, regardless of whether
# or not we're in user space or kernel space.
GRANARY_COMMON_FLAGS += -fPIC

# Enable various warnings and errors.
GRANARY_COMMON_WARN_FLAGS += -Wall -Wextra -Werror -pedantic -pedantic-errors
GRANARY_COMMON_WARN_FLAGS += -Wcast-qual -Wshadow -Wpointer-arith
GRANARY_COMMON_WARN_FLAGS += -Wconversion -Wuninitialized -Winit-self 
GRANARY_COMMON_WARN_FLAGS += -Wmissing-declarations -Wwrite-strings -Wfloat-equal
GRANARY_COMMON_WARN_FLAGS += -fstrict-aliasing -Wstrict-aliasing=2 -Wstrict-overflow=2
GRANARY_COMMON_WARN_FLAGS += -Wcast-align -Wredundant-decls -Wvolatile-register-var
GRANARY_COMMON_WARN_FLAGS += -Wstack-protector -Wold-style-cast

# Disable various warnings and errors.
GRANARY_COMMON_WARN_FLAGS += -Wno-nested-anon-types -Wno-variadic-macros
GRANARY_COMMON_WARN_FLAGS += -Wno-gnu -Wno-c11-extensions -Wno-long-long

# Add the common flags to both the C++ and C compiler flags.
GRANARY_CXX_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_WARN_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_CC_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_ASM_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS)
GRANARY_LD_FLAGS += -fPIC $(GRANARY_COMMON_ARCH_FLAGS)

# Mask some of the libc and libc++ symbols with Granary equivalents.
ifeq (0,$(GRANARY_STANDALONE))
	GRANARY_LD_FLAGS_LATE += "-Wl,-flat_namespace"
endif
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=memset=granary_memset"
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=memcpy=granary_memcpy"
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=memcmp=granary_memcmp"
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__builtin_memcpy=granary_memcpy"

# Handle global `operator delete(void*)`.
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=_ZdlPv=granary_break_on_unreachable_code"

# Handle global `operator new(unsigned long)`.
GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=_Znwm=granary_break_on_unreachable_code"

# Handle some other C/C++ runtime library functions.
ifeq (kernel,$(GRANARY_WHERE))
    GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_atexit=granary_break_on_unreachable_code"
    GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_guard_acquire=granary_break_on_unreachable_code"
    GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_guard_release=granary_break_on_unreachable_code"
    GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_guard_abort=granary_break_on_unreachable_code"
    GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_pure_virtual=granary_break_on_unreachable_code"
	# TODO(pag): __cxx_global_var_init for all static global variables?
endif

GRANARY_LINT_DIR = $(GRANARY_SRC_DIR)/dependencies/cpplint
GRANARY_LINT_FLAGS = --verbose=5
GRANARY_LINT = $(GRANARY_PYTHON) $(GRANARY_LINT_DIR)/cpplint.py $(GRANARY_LINT_FLAGS)

.SILENT :
