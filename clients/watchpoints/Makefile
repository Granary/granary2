# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := watchpoints
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o
CLIENT_OBJS = $(CLIENT_BIN_DIR)/watchpoints.o $(CLIENT_BIN_DIR)/type_id.o

$(CLIENT_OBJ): $(CLIENT_OBJS)
	@echo "Building object $@."
	@$(GRANARY_LD) -g -r $^ -o $@

all: $(CLIENT_OBJ)
