# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all clean

include Makefile.inc

# Include the `Makefile.inc`s of every tool.
GRANARY_TOOL_INCS := $(addsuffix /Makefile.inc,$(GRANARY_TOOLS))
GRANARY_TOOL_INCS := $(addprefix $(GRANARY_TOOL_DIR)/,$(GRANARY_TOOL_INCS))
include $(GRANARY_TOOL_INCS)

GRANARY_LD_FLAGS_EARLY += -shared
GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"
GRANARY_LD_FLAGS_LATE += "-Wl,-Bdynamic"

GRANARY_CXX_FLAGS += -fvisibility=hidden
GRANARY_CC_FLAGS += -fvisibility=hidden

# Generate a rule to build the LLVM bitcode files for a specific tool.
define GENRULE
$(1)-all : $(addprefix $(GRANARY_TOOL_DIR)/$(1)/,$($(1)-objs))
	@echo "Building $(GRANARY_BIN_DIR)/lib$(1).so"
	
	# Link the bitcode files together.
	@llvm-link $(addprefix $(GRANARY_TOOL_DIR)/$(1)/,$($(1)-objs)) \
		-o $(GRANARY_TOOL_DIR)/$(1)/lib$(1).ll
	
	# Convert the linked bitcode files into a single object file. Goal here is
	# to take advantage of optimization on the now fully-linked file.
	@$(GRANARY_CC) $(GRANARY_LD_FLAGS) \
		-c $(GRANARY_TOOL_DIR)/$(1)/lib$(1).ll \
		-o $(GRANARY_TOOL_DIR)/$(1)/lib$(1).o
	
	# Compile the object file into a shared library.
	# TODO(pag): Make a kernel space equivalent, e.g. output `.S` above instead
	#            of a `.o`.
	@$(GRANARY_CC) \
		$(GRANARY_LD_FLAGS) \
		$(GRANARY_LD_FLAGS_EARLY) \
		$(GRANARY_TOOL_DIR)/$(1)/lib$(1).o $(GRANARY_BIN_DIR)/tool.o \
		$(GRANARY_LD_FLAGS_LATE) \
		-o $(GRANARY_BIN_DIR)/lib$(1).so

endef

# Generate rules to build the bitcode files for each tool.
$(foreach tool,$(GRANARY_TOOLS),$(eval $(call GENRULE,$(tool))))
#$(foreach tool,$(GRANARY_TOOLS),$(info $(call GENRULE,$(tool))))

# Compile C++ files to LLVM bitcode.
$(GRANARY_TOOL_DIR)/%.ll: $(GRANARY_TOOL_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -c $< -o $@
	
# Clean a specific tool.
%-clean:
	@echo "Cleaning tool $* in $(GRANARY_TOOL_DIR)/$*"
	@find $(GRANARY_TOOL_DIR)/$* -type f -name \*.ll -execdir rm {} \;
	@find $(GRANARY_TOOL_DIR)/$* -type f -name \*.o -execdir rm {} \;

# Build all tools specified in `GRANARY_TOOLS`.
all: $(addsuffix -all,$(GRANARY_TOOLS))

# Clean all tools specified in `GRANARY_TOOLS`.	
clean: $(addsuffix -clean,$(GRANARY_TOOLS))
	