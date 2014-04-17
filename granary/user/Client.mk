# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all clean

include ../../Makefile.inc

# Include the `Makefile.inc`s of every client.
GRANARY_CLIENT_INCS := $(addsuffix /Makefile.inc,$(GRANARY_CLIENTS))
GRANARY_CLIENT_INCS := $(addprefix $(GRANARY_CLIENT_DIR)/,$(GRANARY_CLIENT_INCS))
include $(GRANARY_CLIENT_INCS)

GRANARY_LD_FLAGS_EARLY += -shared
GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"
GRANARY_LD_FLAGS_LATE += "-Wl,-Bdynamic"
GRANARY_LD_FLAGS_LATE += "-Wl,--export-dynamic"

GRANARY_CXX_FLAGS += -fvisibility=hidden
GRANARY_CC_FLAGS += -fvisibility=hidden

# Generate a rule to build the LLVM bitcode files for a specific client.
define GENRULE
$(1)-all : $(addprefix $(GRANARY_CLIENT_DIR)/$(1)/,$($(1)-objs))
	@echo "Building $(GRANARY_BIN_DIR)/lib$(1).so"
	
	# Link the bitcode files together.
	@llvm-link $(addprefix $(GRANARY_CLIENT_DIR)/$(1)/,$($(1)-objs)) \
		-o $(GRANARY_CLIENT_DIR)/$(1)/lib$(1).bc
	
	# Convert the linked bitcode files into a single object file. Goal here is
	# to take advantage of optimization on the now fully-linked file.
	@$(GRANARY_CC) -Qunused-arguments $(GRANARY_CC_FLAGS) \
		-c $(GRANARY_CLIENT_DIR)/$(1)/lib$(1).bc \
		-o $(GRANARY_CLIENT_DIR)/$(1)/lib$(1).o
	
	# Compile the object file into a shared library.
	@$(GRANARY_CC) \
		$(GRANARY_LD_FLAGS) \
		$(GRANARY_LD_FLAGS_EARLY) \
		$(GRANARY_CLIENT_DIR)/$(1)/lib$(1).o $(GRANARY_CLIENT_OBJ) \
		$(GRANARY_LD_FLAGS_LATE) \
		-o $(GRANARY_BIN_DIR)/lib$(1).so

endef

# Generate rules to build the bitcode files for each client.
$(foreach client,$(GRANARY_CLIENTS),$(eval $(call GENRULE,$(client))))
#$(foreach client,$(GRANARY_CLIENTS),$(info $(call GENRULE,$(client))))

# Compile C++ files to LLVM bitcode.
$(GRANARY_CLIENT_DIR)/%.bc: $(GRANARY_CLIENT_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -flto -c $< -o $@
	
# Clean a specific client.
%-clean:
	@echo "Cleaning client $* in $(GRANARY_CLIENT_DIR)/$*"
	@find $(GRANARY_CLIENT_DIR)/$* -type f -name \*.bc -execdir rm {} \;
	@find $(GRANARY_CLIENT_DIR)/$* -type f -name \*.o -execdir rm {} \;

# Build all clients specified in `GRANARY_CLIENTS`.
all: $(addsuffix -all,$(GRANARY_CLIENTS))

# Clean all clients specified in `GRANARY_CLIENTS`.	
clean: $(addsuffix -clean,$(GRANARY_CLIENTS))
	