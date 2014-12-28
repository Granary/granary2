# Copyright 2014 Peter Goodman, all rights reserved.

include Makefile.inc

.PHONY: all clean clean_generated test
.PHONY: clients clean_clients
.PHONY: where_common where_user where_kernel
.PHONY: target_debug target_release target_test
.PHONY: build_driver build_dbt build_arch build_os

# Make a header file that external clients can use to define clients. This
# happens before `build_clients`.
$(GRANARY_HEADERS):
	@mkdir -p $(GRANARY_HEADERS_DIR)
	# Make the combined C++ header file (granary.h) used by clients.
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/generate_export_headers.py \
		$(GRANARY_WHERE) $(GRANARY_SRC_DIR) $(GRANARY_HEADERS_DIR) \
		"$(GRANARY_HEADER_MACRO_DEFS)"
	
	# Copy some arch-specific headers to clients so they can use assembly
	# routines.
	@cp $(GRANARY_ARCH_SRC_DIR)/asm/include.asm.inc \
		$(GRANARY_HEADERS_DIR)/include.asm.inc

# Makefile that Granary and external clients can used to access user / kernel
# headers. This happens before `build_os`.
$(GRANARY_OS_TYPES):
	@echo "Entering $(GRANARY_SRC_DIR)/os/$(GRANARY_OS)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/os/$(GRANARY_OS) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) types

build_deps:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_dbt: build_deps
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_arch: build_deps
	@echo "Entering $(GRANARY_ARCH_SRC_DIR)"
	$(MAKE) -C $(GRANARY_ARCH_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_os: build_deps $(GRANARY_OS_TYPES)
	@echo "Entering $(GRANARY_WHERE_SRC_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_headers: $(GRANARY_HEADERS)

# Compile and link all main components into `.o` files that can then be linked
# together into a final executable.
where_common: build_deps build_arch build_dbt build_os build_headers
	@echo "Entering $(GRANARY_SRC_DIR)/clients"
	$(MAKE) -C $(GRANARY_SRC_DIR)/clients \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) exec

# Target-specific.
target_debug: where_common
target_release: where_common
target_opt: where_common
target_test: where_common
	@mkdir -p $(GRANARY_TEST_BIN_DIR)
	@mkdir -p $(GRANARY_GTEST_BIN_DIR)
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

setup:
	@echo "Getting all depency code."
	@$(GRANARY_SH) ./scripts/init_submodules.sh
	@echo "Granary is ready to be compiled."

all: target_$(GRANARY_TARGET)
	@echo "Done."

# Clean up all executable / binary files. This just throws away the bin dir
# that is specific to the target/where variables.
clean:
	@echo "Removing all previously compiled files."
	@-rm -rf $(GRANARY_BIN_DIR) $(GRANARY_DEV_NULL)

# Clean up all auto-generated files.
clean_generated:
	@echo "Removing all previously auto-generated files."
	@find $(GRANARY_GEN_SRC_DIR) -type f -execdir rm {} \;

# Run all test cases.
ifeq (test,$(GRANARY_TARGET))
test: all
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) test
else
test:
	$(MAKE) $(MFLAGS) test GRANARY_TARGET=test
endif
