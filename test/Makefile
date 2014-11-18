# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

GRANARY_IN_TEST_DIR := 1

include $(GRANARY_SRC_DIR)/Makefile.inc

GRANARY_TEST_SRC_FILES = $(shell find $(GRANARY_TEST_SRC_DIR) -type f -name '*.cc')
GRANARY_TEST_OBJ_FILES = $(subst $(GRANARY_TEST_SRC_DIR),$(GRANARY_TEST_BIN_DIR),$(GRANARY_TEST_SRC_FILES:.cc=.o))
GRANARY_TEST_ASM_FILES = $(shell find $(GRANARY_TEST_SRC_DIR) -type f -name '*.S')
GRANARY_TEST_ASM_OBJ_FILES = $(subst $(GRANARY_TEST_SRC_DIR),$(GRANARY_TEST_BIN_DIR),$(GRANARY_TEST_ASM_FILES:.S=.o))
GRANARY_EXE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME).out

# Flags passed to the C++ compiler.
GRANARY_TEST_CXX_FLAGS =  -g3 -Wall -Wextra -pthread
GRANARY_TEST_CXX_FLAGS += -isystem $(GRANARY_GMOCK_SRC_DIR)/include
GRANARY_TEST_CXX_FLAGS += -isystem $(GRANARY_GTEST_SRC_DIR)/include
GRANARY_TEST_CXX_FLAGS += $(GRANARY_COMMON_FLAGS)
GRANARY_TEST_CXX_FLAGS += $(GRANARY_COMMON_CXX_FLAGS)
GRANARY_TEST_CXX_FLAGS += -Wno-missing-field-initializers

GRANARY_GTEST_CXX_FLAGS = $(GRANARY_TEST_CXX_FLAGS) -I$(GRANARY_GTEST_SRC_DIR)
GRANARY_GMOCK_CXX_FLAGS = $(GRANARY_TEST_CXX_FLAGS) -I$(GRANARY_GMOCK_SRC_DIR)

# All Google Test headers.
GRANARY_GTEST_HEADERS =  $(GRANARY_GTEST_SRC_DIR)/include/gtest/*.h
GRANARY_GTEST_HEADERS += $(GRANARY_GTEST_SRC_DIR)/include/gtest/internal/*.h

# All Google Mock headers, including Google Test headers.
GRANARY_GMOCK_HEADERS = $(GRANARY_GTEST_HEADERS)
GRANARY_GMOCK_HEADERS += $(GRANARY_GMOCK_SRC_DIR)/include/gmock/*.h
GRANARY_GMOCK_HEADERS += $(GRANARY_GMOCK_SRC_DIR)/include/gmock/internal/*.h

# All Google Test source files, including headers.
GRANARY_GTEST_SRCS = $(GRANARY_GTEST_HEADERS)
GRANARY_GTEST_SRCS += $(GRANARY_GTEST_SRC_DIR)/src/*.cc
GRANARY_GTEST_SRCS += $(GRANARY_GTEST_SRC_DIR)/src/*.h

# All Google Mock source files, headers and Google Test header and source files.
GRANARY_GMOCK_SRCS = $(GRANARY_GTEST_SRCS) $(GRANARY_GMOCK_HEADERS)
GRANARY_GMOCK_SRCS += $(GRANARY_GMOCK_SRC_DIR)/src/*.cc

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
$(GRANARY_GTEST_BIN_DIR)/gtest-all.o: $(GRANARY_GTEST_SRCS)
	@echo "Building CXX object $(GRANARY_GTEST_BIN_DIR)/gtest-all.o"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_GTEST_CXX_FLAGS) \
		-c $(GRANARY_GTEST_SRC_DIR)/src/gtest-all.cc \
		-o $(GRANARY_GTEST_BIN_DIR)/gtest-all.o

$(GRANARY_GMOCK_BIN_DIR)/gmock-all.o: $(GRANARY_GMOCK_SRCS)
	@echo "Building CXX object $(GRANARY_GMOCK_BIN_DIR)/gmock-all.o"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_GMOCK_CXX_FLAGS) \
        -c $(GRANARY_GMOCK_SRC_DIR)/src/gmock-all.cc \
		-o $(GRANARY_GMOCK_BIN_DIR)/gmock-all.o

$(GRANARY_TEST_BIN_DIR)/%.o :: $(GRANARY_TEST_SRC_DIR)/%.S
	@echo "Building ASM object $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) -Qunused-arguments $(GRANARY_ASM_FLAGS) -c $< -o $@

$(GRANARY_TEST_BIN_DIR)/%.o :: $(GRANARY_TEST_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_TEST_CXX_FLAGS) -c $< -o $@

# Compile all individual testers into one large test file/runner.
$(GRANARY_EXE): $(GRANARY_TEST_OBJ_FILES) \
				$(GRANARY_TEST_ASM_OBJ_FILES) \
                $(GRANARY_GTEST_BIN_DIR)/gtest-all.o \
                $(GRANARY_GMOCK_BIN_DIR)/gmock-all.o \
                $(GRANARY_BIN_DIR)/libgranary.o
	@echo "Compiling $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_GMOCK_CXX_FLAGS) -lpthread -ldl $^ -o $@

# Build all test targets.
all: $(GRANARY_EXE)

# Run all test targets.
test: all
	$(GRANARY_EXE)
