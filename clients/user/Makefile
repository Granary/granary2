# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := user
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o
CLIENT_DEP_OBJS := $(CLIENT_BIN_DIR)/user.o $(CLIENT_BIN_DIR)/syscall.o
CLIENT_DEP_OBJS += $(CLIENT_BIN_DIR)/signal.o
$(CLIENT_OBJ): $(CLIENT_DEP_OBJS)
	@echo "Building combined object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LD) -g -r $^ -o $@

ifeq (user,$(GRANARY_WHERE))
all: $(CLIENT_OBJ)
else
all:
endif
