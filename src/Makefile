# FileSearch Makefile
#
# Cross-platform build for Windows, macOS, and Linux
#
# Usage:
#   make          - Build the program
#   make clean    - Remove built files
#   make debug    - Build with debug symbols
#
# SQLite:
#   Place sqlite3.c and sqlite3.h in the ../deps/ directory.
#   Download from: https://www.sqlite.org/download.html (amalgamation)
#

# Detect operating system
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    TARGET = filesearch.exe
    RM = del /Q
    LDFLAGS = -lm
    # For MSYS2/MinGW, override RM to use Unix-style rm
    ifdef MSYSTEM
        RM = rm -f
    endif
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := macOS
        LDFLAGS = -lm
    else
        DETECTED_OS := Linux
        LDFLAGS = -lpthread -ldl -lm
    endif
    TARGET = filesearch
    RM = rm -f
endif

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=c99

# Directories
DEPS_DIR = ../deps

# SQLite source (amalgamation)
SQLITE_SRC = $(DEPS_DIR)/sqlite3.c
SQLITE_OBJ = $(DEPS_DIR)/sqlite3.o
SQLITE_FLAGS = -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_FTS5

# Source files
SRCS = main.c utils.c database.c paths.c tags.c categories.c search.c
OBJS = $(SRCS:.c=.o)

# Include path for sqlite3.h
INCLUDES = -I$(DEPS_DIR)

# Default target
all: CFLAGS += -O2
all: $(TARGET)
	@echo "Built for $(DETECTED_OS)"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)
	@echo "Debug build for $(DETECTED_OS)"

# Link
$(TARGET): $(OBJS) $(SQLITE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile application source files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Compile SQLite amalgamation
$(SQLITE_OBJ): $(SQLITE_SRC)
	$(CC) $(CFLAGS) $(SQLITE_FLAGS) -c -o $@ $<

# Dependencies
main.o: main.c utils.h database.h paths.h tags.h categories.h search.h
utils.o: utils.c utils.h
database.o: database.c database.h utils.h
paths.o: paths.c paths.h utils.h database.h
tags.o: tags.c tags.h utils.h database.h
categories.o: categories.c categories.h utils.h database.h
search.o: search.c search.h utils.h database.h tags.h

# Clean application objects
clean:
	$(RM) $(OBJS) $(TARGET)

# Clean including SQLite object
cleanall:
	$(RM) $(OBJS) $(SQLITE_OBJ) $(TARGET)

# Install (Unix only)
PREFIX ?= /usr/local
install: $(TARGET)
ifneq ($(DETECTED_OS),Windows)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/
else
	@echo "Install target not supported on Windows. Copy $(TARGET) manually."
endif

# Check if SQLite source exists
$(SQLITE_SRC):
	@echo "Error: SQLite amalgamation not found at $(SQLITE_SRC)"
	@echo "Download from: https://www.sqlite.org/download.html"
	@echo "Extract sqlite3.c and sqlite3.h to the $(DEPS_DIR)/ directory"
	@exit 1

.PHONY: all debug clean cleanall install
