
.PHONY: all

include $(GRANARY_SRC_DIR)/Makefile.inc

XED_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/xed2-intel64
XED_BIN_DIR = $(GRANARY_BIN_DIR)/dependencies/xed2-intel64

XED_LIB_DIR = $(GRANARY_SRC_DIR)/dependencies/xed2-intel64/lib_$(GRANARY_WHERE)
XED_GEN_DIR = $(GRANARY_SRC_DIR)/generated/xed2-intel64

XED_TOOLS = ambiguous_operands instruction_builder
XED_TOOL_OBJS = $(addprefix $(XED_SRC_DIR)/granary/,$(addsuffix .out,$(XED_TOOLS)))

# Generate rules for each Granary-specific XED tool. Each XED tool generates
# a `.cc` file of the same name.
define GENRULE
$(XED_GEN_DIR)/$(1).out: $(XED_SRC_DIR)/granary/$(1).cc $(XED_LIB_DIR)/libxed.a
	@echo "Building XED tool $(XED_GEN_DIR)/$(1).out"
	@mkdir -p $(XED_GEN_DIR)
	@$(GRANARY_CXX) -std=c++11 -I$(GRANARY_SRC_DIR) -g3 \
		$(XED_SRC_DIR)/granary/$(1).cc $(XED_LIB_DIR)/libxed.a \
		-o $(XED_GEN_DIR)/$(1).out

$(XED_GEN_DIR)/$(1).cc: $(XED_GEN_DIR)/$(1).out
	@$(XED_GEN_DIR)/$(1).out > $(XED_GEN_DIR)/$(1).cc

$(1): $(XED_GEN_DIR)/$(1).cc
endef

$(foreach tool,$(XED_TOOLS),$(eval $(call GENRULE,$(tool))))

# Unarchive libxed, then combine all of its object files into a single unified
# object file.
$(GRANARY_DRIVER_OBJ):
	@mkdir -p $(dir $(GRANARY_DRIVER_OBJ))
	@mkdir -p $(XED_BIN_DIR)
	@cd $(XED_BIN_DIR) ; \
		$(GRANARY_AR) x $(XED_LIB_DIR)/libxed.a ; \
		$(GRANARY_LD) -g -r $(XED_BIN_DIR)/*.o -o $(GRANARY_DRIVER_OBJ)
	
# Compile 
all: $(XED_TOOLS) $(GRANARY_DRIVER_OBJ)
