
.PHONY: all

include ../../Makefile.inc

XED_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/xed2-intel64
XED_GEN_DIR = $(GRANARY_SRC_DIR)/generated/xed2-intel64

XED_TOOLS = ambiguous_operands instruction_builder
XED_TOOL_OBJS = $(addprefix $(XED_SRC_DIR)/granary/,$(addsuffix .out,$(XED_TOOLS)))

# Generate rules for each Granary-specific XED tool. Each XED tool generates
# a `.cc` file of the same name.
define GENRULE
$(XED_GEN_DIR)/$(1).out: $(XED_SRC_DIR)/granary/$(1).cc $(XED_SRC_DIR)/lib/libxed.a
	@echo "Building XED tool $(XED_GEN_DIR)/$(1).out"
	@mkdir -p $(XED_GEN_DIR)
	@$(GRANARY_CXX) -std=c++11 -I$(GRANARY_SRC_DIR) \
		$(XED_SRC_DIR)/granary/$(1).cc $(XED_SRC_DIR)/lib/libxed.a \
		-o $(XED_GEN_DIR)/$(1).out

$(XED_GEN_DIR)/$(1).cc: $(XED_GEN_DIR)/$(1).out
	@$(XED_GEN_DIR)/$(1).out > $(XED_GEN_DIR)/$(1).cc

$(1): $(XED_GEN_DIR)/$(1).cc
endef

$(foreach tool,$(XED_TOOLS),$(eval $(call GENRULE,$(tool))))

# Compile 
all: $(XED_TOOLS)
	@mkdir -p $(GRANARY_BIN_DIR)
	@cp $(XED_SRC_DIR)/lib/libxed.a $(GRANARY_BIN_DIR)