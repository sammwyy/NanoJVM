CC = gcc
AR = ar
JAVAC ?= javac
JAVAC_FLAGS = -Xlint:-options
CFLAGS = -std=c99 -Wall -Wextra -Iinclude -Iinternal -I. -D_POSIX_C_SOURCE=200809L
LDFLAGS =

BUILD_DIR = build/native

LIB_SRC = \
	core/vm.c \
	core/bytecode.c \
	core/classfile.c \
	core/runtime.c \
	core/heap.c \
	core/gc.c \
	core/types.c \
	core/utils.c \
	loader/jar.c \
	loader/zip.c \
	loader/resource.c \
	loader/stream.c \
	cldc/lang.c \
	cldc/util.c \
	cldc/io.c

LIB_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRC))
MAIN_OBJ = $(BUILD_DIR)/main.o

.PHONY: all clean java cldc-classes

all: $(BUILD_DIR)/libnanojvm.a nanojvm

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libnanojvm.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

nanojvm: $(MAIN_OBJ) $(BUILD_DIR)/libnanojvm.a
	$(CC) $(LDFLAGS) -o $@ $(MAIN_OBJ) -L$(BUILD_DIR) -lnanojvm

$(MAIN_OBJ): main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) nanojvm

JAVA_SRCS := $(shell find samples -name "*.java")
CLDC_SRCS := $(shell find cldc/classes -name "*.java")

java:
	@for f in $(JAVA_SRCS); do \
		dir=$$(dirname $$f); \
		$(JAVAC) $(JAVAC_FLAGS) -d $$dir $$f; \
	done

cldc-classes:
	bash cldc/gen_headers.sh
