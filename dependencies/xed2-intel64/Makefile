
.PHONY: all

include ../../Makefile.inc

XED_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/xed2-intel64
XED_GEN_DIR = $(GRANARY_SRC_DIR)/generated/xed2-intel64

XED_TOOLS = make_ir
XED_TOOL_OBJS = $(addprefix $(XED_SRC_DIR)/granary/,$(addsuffix .out,$(XED_TOOLS)))

# Generate a rule to build the individual XED tools that Granary needs.
define GENRULE
$(XED_SRC_DIR)/granary/$(1).out: $(XED_SRC_DIR)/granary/$(1).cc $(XED_SRC_DIR)/lib/libxed.a
	@echo "Building XED tool $(XED_SRC_DIR)/granary/$(1).out"
	@$(GRANARY_CXX) -std=c++11 $(XED_SRC_DIR)/granary/$(1).cc \
		-o $(XED_SRC_DIR)/granary/$(1).out
endef

$(foreach tool,$(XED_TOOLS),$(eval $(call GENRULE,$(tool))))

# Auto-generate an instruction building API.
#$(XED_GEN_DIR)/

# Compile 
all:
	@cp $(XED_SRC_DIR)/lib/libxed.a $(GRANARY_BIN_DIR)