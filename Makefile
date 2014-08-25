# Copyright 2014 Peter Goodman, all rights reserved.

include Makefile.inc

.PHONY: all clean clean_generated test
.PHONY: clients clean_clients
.PHONY: where_common where_user where_kernel
.PHONY: target_debug target_release target_test
.PHONY: build_driver build_dbt build_arch build_os

build_driver:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_dbt: build_driver
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_arch: build_driver
	@echo "Entering $(GRANARY_ARCH_SRC_DIR)"
	$(MAKE) -C $(GRANARY_ARCH_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_os: build_driver
	@echo "Entering $(GRANARY_WHERE_SRC_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

# Make a header file that external clients can use to define clients.
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

# Generate rules for each Granary client.
define GENRULE_BUILD_CLIENT
.PHONY: build_client_$(1)
build_client_$(1): $(GRANARY_HEADERS)
	@echo "Entering $(GRANARY_CLIENT_DIR)/$(1)"
	$(MAKE) -C $(GRANARY_CLIENT_DIR)/$(1) \
		$(MFLAGS) \
		GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		GRANARY_BIN_DIR=$(GRANARY_BIN_DIR) all
endef

$(foreach client,$(GRANARY_CLIENTS),$(eval $(call GENRULE_BUILD_CLIENT,$(client))))

build_clients: $(addprefix build_client_,$(GRANARY_CLIENTS))

GRANARY_CLIENTS_TARGET = build_clients
ifeq (test,$(GRANARY_TARGET))
	GRANARY_CLIENTS_TARGET :=
endif

# Compile and link all main components into `.o` files that can then be linked
# together into a final executable.
where_common: build_driver build_arch build_dbt build_os $(GRANARY_CLIENTS_TARGET)
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) exec

# Target-specific.
target_debug: where_common
target_release: where_common
target_test: where_common
	@mkdir -p $(GRANARY_TEST_BIN_DIR)
	@mkdir -p $(GRANARY_GTEST_BIN_DIR)
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

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
test: all
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) test
