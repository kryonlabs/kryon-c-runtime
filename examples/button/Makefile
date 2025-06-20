# Makefile for C KRB Button Example

# Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I../../include
LDFLAGS = -lraylib -lm

# Project Specifics
TARGET = button_example
SOURCES = main.c ../../src/krb_reader.c ../../src/raylib_renderer.c

# KRB File and Header Paths
KRB_SOURCE = ../../../kryon-core/examples/button.krb
KRB_HEADER = button_krb_data.h

# Detect Raylib with pkg-config if available
HAS_PKGCONFIG := $(shell command -v pkg-config 2> /dev/null)
ifdef HAS_PKGCONFIG
    RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
    RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
    
    ifneq ($(RAYLIB_CFLAGS),)
        CFLAGS += $(RAYLIB_CFLAGS)
        LDFLAGS += $(RAYLIB_LIBS)
    else
        $(warning Warning: pkg-config could not find Raylib)
    endif
else
    $(warning Warning: pkg-config not found. Ensure Raylib is correctly installed)
endif

# Targets
.PHONY: all clean run data

# Default target
all: $(TARGET)

# Generate KRB data header
$(KRB_HEADER): $(KRB_SOURCE)
	@echo "Generating KRB data header from $(KRB_SOURCE)..."
	# Generate the array and length variable using xxd
	@xxd -i $< > $@
	# Extract the actual variable names generated by xxd
	@VAR_NAME=$$(grep -o '^unsigned char [^[]*' $@ | sed 's/unsigned char //'); \
	VAR_LEN=$$(grep -o '^unsigned int [^;]*' $@ | sed 's/unsigned int //'); \
	echo "// Helper functions" >> $@; \
	echo "static inline unsigned char *get_embedded_krb_data() { return $$VAR_NAME; }" >> $@; \
	echo "static inline unsigned int get_embedded_krb_data_len() { return $$VAR_LEN; }" >> $@
	
# Compile the executable
$(TARGET): $(SOURCES) $(KRB_HEADER) ../../include/renderer.h ../../include/krb.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

# Run the example
run: $(TARGET)
	./$(TARGET)

# Regenerate KRB data header explicitly
data: $(KRB_HEADER)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(KRB_HEADER)