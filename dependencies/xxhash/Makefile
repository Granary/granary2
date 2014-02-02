
.PHONY: all

include ../../Makefile.inc

XX_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/xxhash
XX_BIN_DIR = $(GRANARY_BIN_DIR)/dependencies/xxhash

XX_CC_SRC_FILES = $(shell find $(XX_SRC_DIR) -type f -name '*.cc')
XX_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(XX_CC_SRC_FILES:.cc=.ll))

# Compile the C++ xxHash dependencies.
$(XX_BIN_DIR)/%.ll: $(XX_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_COMMON_CXX_FLAGS) $(GRANARY_COMMON_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS) -emit-llvm -c $< -o $@

# Compile the xxHash dependencies into .ll files.
all: $(XX_OBJ_FILES)
