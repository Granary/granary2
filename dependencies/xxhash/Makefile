
.PHONY: all

include ../../Makefile.inc

XX_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/xxhash
XX_BIN_DIR = $(GRANARY_BIN_DIR)/dependencies/xxhash

XX_CC_SRC_FILES = $(shell find $(XX_SRC_DIR) -type f -name '*.cc')
XX_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(XX_CC_SRC_FILES:.cc=.bc))

# Compile the C++ xxHash dependencies.
$(XX_BIN_DIR)/%.bc: $(XX_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	$(GRANARY_CXX) \
		$(GRANARY_COMMON_CXX_FLAGS) $(GRANARY_COMMON_FLAGS) \
		$(GRANARY_COMMON_ARCH_FLAGS) -emit-llvm -flto -c $< -o $@

# Compile the xxHash dependencies into .bc files.
all: $(XX_OBJ_FILES)
