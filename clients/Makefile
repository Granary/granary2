# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include $(GRANARY_SRC_DIR)/Makefile.inc

# Generate rules to build each Granary client.
define GENRULE_BUILD_CLIENT
.PHONY: build_client_$(1)

$(eval CLIENT_$(1)_MFLAGS := \
	$(MFLAGS) \
	GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
	GRANARY_BIN_DIR=$(GRANARY_BIN_DIR) \
	CLIENT_GEN_DIR=$(GRANARY_SRC_DIR)/generated/clients/$(1) \
	CLIENT_NAME=$(1) \
	CLIENT_OBJ=$(GRANARY_BIN_DIR)/$(1)_client.bc)

build_client_$(1):
	@echo "Entering $(GRANARY_CLIENTS_SRC_DIR)/$(1)"
	$(MAKE) -C $(GRANARY_CLIENTS_SRC_DIR)/$(1) generated \
		$(CLIENT_$(1)_MFLAGS)
	$(MAKE) -C $(GRANARY_CLIENTS_SRC_DIR)/$(1) all \
		$(CLIENT_$(1)_MFLAGS)
endef

$(foreach client,$(GRANARY_CLIENTS),$(eval $(call GENRULE_BUILD_CLIENT,$(client))))

GRANARY_CLIENT_BC = $(GRANARY_BIN_DIR)/clients.bc

CLIENT_OBJS = $(foreach client,$(GRANARY_CLIENTS),$(GRANARY_BIN_DIR)/$(client)_client.bc)
CLIENT_TARGETS = $(addprefix build_client_,$(GRANARY_CLIENTS))

# Link together all client bitcode files into a single large bitcode file.
$(GRANARY_CLIENT_OBJ): $(CLIENT_TARGETS)
	@echo "Linking client bitcode files into $@."
	@$(GRANARY_LLVM_LINK) \
		$(CLIENT_OBJS) \
		-o $(GRANARY_CLIENT_BC)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) \
		-c $(GRANARY_CLIENT_BC) \
		-o $@

# Lint all client code.
lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) \
		$(shell find $(GRANARY_SRC_DIR)/clients -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_CLIENT_OBJ)
