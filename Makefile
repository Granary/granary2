
include Makefile.inc

.PHONY: all clean clean_generated export

# Compile all files.
all:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) $(MFLAGS) all
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary $(MFLAGS) all
	@echo "Entering $(GRANARY_WHERE_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_DIR) $(MFLAGS) all
	@echo "Done."

# Clean up all executable / binary files.
clean:
	@find $(GRANARY_BIN_DIR) -type f -name \*.so -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.ll -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.o -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.o.cmd -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.S -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.out -execdir rm {} \;

# Clean up all auto-generated files.
clean_generated:
	@find $(GRANARY_GEN_SRC_DIR) -type f -execdir rm {} \;

# Make a header file that external tools can use to define tools.
headers:
	