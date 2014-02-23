# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../Makefile.inc

GRANARY_TEST_SRC_FILES = $(shell find $(GRANARY_TEST_SRC_DIR) -type f -name '*_unittest.cc')
GRANARY_TEST_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_TEST_SRC_FILES:.cc=.o))
GRANARY_TEST_EXE_FILES = $(GRANARY_TEST_OBJ_FILES:.o=.out)

# Flags passed to the C++ compiler.
GRANARY_TEST_CXX_FLAGS =
GRANARY_TEST_CXX_FLAGS += -g3 -Wall -Wextra -pthread
GRANARY_TEST_CXX_FLAGS += -isystem $(GRANARY_GTEST_SRC_DIR)/include
GRANARY_TEST_CXX_FLAGS += -I$(GRANARY_GTEST_SRC_DIR)
GRANARY_TEST_CXX_FLAGS += $(GRANARY_COMMON_CXX_FLAGS) $(GRANARY_COMMON_FLAGS)
GRANARY_TEST_CXX_FLAGS += -Wno-missing-field-initializers

# All Google Test headers.
GRANARY_GTEST_HEADERS = 
GRANARY_GTEST_HEADERS += $(GRANARY_GTEST_SRC_DIR)/include/gtest/*.h
GRANARY_GTEST_HEADERS += $(GRANARY_GTEST_SRC_DIR)/include/gtest/internal/*.h

# All Google Test source files.
GRANARY_GTEST_SRCS = 
GRANARY_GTEST_SRCS += $(GRANARY_GTEST_SRC_DIR)/src/*.cc
GRANARY_GTEST_SRCS += $(GRANARY_GTEST_SRC_DIR)/src/*.h
GRANARY_GTEST_SRCS += $(GRANARY_GTEST_HEADERS)

# Generate a test runner rule. Having one runner rule per test allows us to
# run tests in parallel.
define GENRULE
$(1)-run : $(1)
	@echo "Running test $(1)"
	@$(1)
endef

# Generate rules to build the bitcode files for each tool.
$(foreach test,$(GRANARY_TEST_EXE_FILES),$(eval $(call GENRULE,$(test))))

# List of all test runners.
GRANARY_TEST_RUNNERS = $(addsuffix -run,$(GRANARY_TEST_EXE_FILES))

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
$(GRANARY_GTEST_BIN_DIR)/gtest-all.o: $(GRANARY_GTEST_SRCS)
	@echo "Building CXX object $(GRANARY_GTEST_BIN_DIR)/gtest-all.o"
	@$(GRANARY_CXX) $(GRANARY_TEST_CXX_FLAGS) \
		-c $(GRANARY_GTEST_SRC_DIR)/src/gtest-all.cc \
		-o $(GRANARY_GTEST_BIN_DIR)/gtest-all.o

$(GRANARY_GTEST_BIN_DIR)/gtest_main.o: $(GRANARY_GTEST_SRCS)
	@echo "Building CXX object $(GRANARY_GTEST_BIN_DIR)/gtest_main.o"
	@$(GRANARY_CXX) $(GRANARY_TEST_CXX_FLAGS) \
		-c $(GRANARY_GTEST_SRC_DIR)/src/gtest_main.cc \
		-o $(GRANARY_GTEST_BIN_DIR)/gtest_main.o

$(GRANARY_GTEST_BIN_DIR)/gtest.a: $(GRANARY_GTEST_BIN_DIR)/gtest-all.o
	@echo "Creating archive $(GRANARY_GTEST_BIN_DIR)/gtest.a"
	@$(GRANARY_AR) cr $(GRANARY_GTEST_BIN_DIR)/gtest.a \
		$(GRANARY_GTEST_BIN_DIR)/gtest-all.o

$(GRANARY_GTEST_BIN_DIR)/gtest_main.a: $(GRANARY_GTEST_BIN_DIR)/gtest-all.o $(GRANARY_GTEST_BIN_DIR)/gtest_main.o
	@echo "Creating archive $(GRANARY_GTEST_BIN_DIR)/gtest_main.a"
	@$(GRANARY_AR) cr $(GRANARY_GTEST_BIN_DIR)/gtest_main.a \
		$(GRANARY_GTEST_BIN_DIR)/gtest-all.o \
		$(GRANARY_GTEST_BIN_DIR)/gtest_main.o

$(GRANARY_TEST_BIN_DIR)/%.o: $(GRANARY_TEST_SRC_DIR)/%.cc $(GRANARY_GTEST_HEADERS)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_TEST_CXX_FLAGS) -c $< -o $@

$(GRANARY_TEST_BIN_DIR)/%.out: $(GRANARY_TEST_BIN_DIR)/%.o $(GRANARY_GTEST_BIN_DIR)/gtest_main.a $(GRANARY_MERGED_OBJ)
	@echo "Compiling $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_TEST_CXX_FLAGS) -lpthread -ldl $^ -o $@
	
# Build all test targets.
all: $(GRANARY_TEST_EXE_FILES)

# Run all test targets.
test: $(GRANARY_TEST_RUNNERS)
	
