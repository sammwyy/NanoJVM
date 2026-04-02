CC = gcc
AR = ar
JAVAC ?= javac
CFLAGS = -std=c99 -Wall -Wextra -Iinclude -Iinternal -I.
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
	cldc/io.c \
	midp/midlet.c \
	midp/lcdui.c \
	midp/rms.c \
	midp/net.c \
	m3g/scene.c \
	m3g/math.c \
	m3g/animation.c \
	m3g/mesh.c \
	m3g/render.c \
	platform/wasm/wasm.c \
	platform/desktop/desktop.c \
	platform/headless/headless.c

LIB_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRC))
MAIN_OBJ = $(BUILD_DIR)/main.o

.PHONY: all clean java

all: $(BUILD_DIR)/libjmevm.a jmevm

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libjmevm.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

jmevm: $(MAIN_OBJ) $(BUILD_DIR)/libjmevm.a
	$(CC) $(LDFLAGS) -o $@ $(MAIN_OBJ) -L$(BUILD_DIR) -ljmevm

$(MAIN_OBJ): main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) jmevm

JAVA_SRCS := $(wildcard samples/*.java)

java:
	@$(JAVAC) -d samples $(JAVA_SRCS)
