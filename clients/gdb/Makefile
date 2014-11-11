# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all generated 

include $(GRANARY_SRC_DIR)/Makefile.inc

FIND_SYM := $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py

$(CLIENT_GEN_DIR)/offsets.h:
	@mkdir -p $(@D)
	@$(GRANARY_PYTHON) $(FIND_SYM) _dl_debug_state ld >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) __nptl_create_event libpthread >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) __nptl_death_event libpthread >> $(@)

all: $(CLIENT_OBJ)
generated: $(CLIENT_GEN_DIR)/offsets.h
