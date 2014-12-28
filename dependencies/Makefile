# Copyright 2014 Peter Goodman, all rights reserved.

include $(GRANARY_SRC_DIR)/Makefile.inc

.PHONY: all

GRANARY_HASH_OBJ := $(GRANARY_BIN_DIR)/dependencies/fasthash/fasthash.o

$(GRANARY_DRIVER_OBJ):
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

$(GRANARY_HASH_OBJ):
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/fasthash"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/fasthash \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

$(GRANARY_DEPENDENCIES_OBJ): $(GRANARY_DRIVER_OBJ) $(GRANARY_HASH_OBJ)
	@$(GRANARY_LD) -g -r $^ -o $@
	
all: $(GRANARY_DEPENDENCIES_OBJ)
