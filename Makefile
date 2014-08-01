# Copyright 2014 Peter Goodman, all rights reserved.

include Makefile.inc

.PHONY: all headers clean clean_generated test
.PHONY: clients clean_clients
.PHONY: where_common where_user where_kernel
.PHONY: target_debug target_release target_test

build_deps:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_bdt:
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

build_os:
	@echo "Entering $(GRANARY_WHERE_SRC_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

# Generate rules for each Granary client.
define GENRULE_BUILD_CLIENT
build_client_$(1):
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
where_common: build_deps build_bdt build_os $(GRANARY_CLIENTS_TARGET)
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) exec

# Make a final object that clients can link against for getting arch-specific
# implementations of built-in compiler functions that are also sometimes
# synthesized by optimizing compilers (e.g. memset).
where_user: where_common
	
# We handle the equivalent of `user_tool` in `granary/kernel/Took.mk`.
where_kernel: where_common

# Target-specific.
target_debug: where_$(GRANARY_WHERE)
target_release: where_$(GRANARY_WHERE)
target_test: target_debug
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

# Make a header file that external clients can use to define clients.
headers:
	@mkdir -p $(GRANARY_EXPORT_HEADERS_DIR)
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/generate_export_headers.py \
		$(GRANARY_WHERE) $(GRANARY_SRC_DIR) $(GRANARY_EXPORT_HEADERS_DIR) \
		"$(GRANARY_HEADER_MACRO_DEFS)"

# Run all test cases.
test: all
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) test
