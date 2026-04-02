#include "core/runtime.h"
#include "core/heap.h"

#include <stdio.h>
#include <string.h>

#define JMEVM_REF_PRINTSTREAM 0x1000

#define JMEVM_MAX_NATIVE_METHODS 16

static struct jmevm_native_method g_native_methods[JMEVM_MAX_NATIVE_METHODS];
static size_t g_native_methods_count = 0;
static int g_native_inited = 0;

static int bytes_utf8_equals(const uint8_t *a, size_t a_len, const char *b) {
  if (a == NULL || b == NULL) {
    return 0;
  }
  size_t b_len = strlen(b);
  if (a_len != b_len) {
    return 0;
  }
  return memcmp(a, b, b_len) == 0;
}

int jmevm_native_register(const char *class_name, const char *method_name,
                          const char *descriptor, jmevm_native_fn fn) {
  if (class_name == NULL || method_name == NULL || descriptor == NULL ||
      fn == NULL) {
    return -1;
  }
  if (g_native_methods_count >= JMEVM_MAX_NATIVE_METHODS) {
    return -1;
  }

  g_native_methods[g_native_methods_count++] = (struct jmevm_native_method){
      .class_name = class_name,
      .method_name = method_name,
      .descriptor = descriptor,
      .fn = fn,
  };
  return 0;
}

static const struct jmevm_native_method *
native_lookup_exact(const uint8_t *class_bytes, size_t class_len,
                    const uint8_t *method_bytes, size_t method_len,
                    const uint8_t *desc_bytes, size_t desc_len) {
  for (size_t i = 0; i < g_native_methods_count; i++) {
    const struct jmevm_native_method *m = &g_native_methods[i];
    if (!bytes_utf8_equals(class_bytes, class_len, m->class_name)) {
      continue;
    }
    if (!bytes_utf8_equals(method_bytes, method_len, m->method_name)) {
      continue;
    }
    if (!bytes_utf8_equals(desc_bytes, desc_len, m->descriptor)) {
      continue;
    }
    return m;
  }
  return NULL;
}

const struct jmevm_native_method *
jmevm_native_lookup_utf8(const uint8_t *class_bytes, size_t class_len,
                         const uint8_t *method_bytes, size_t method_len,
                         const uint8_t *desc_bytes, size_t desc_len) {
  if (class_bytes == NULL || method_bytes == NULL || desc_bytes == NULL) {
    return NULL;
  }
  return native_lookup_exact(class_bytes, class_len, method_bytes, method_len,
                             desc_bytes, desc_len);
}

static int32_t native_println_int(jmevm_vm *vm, int32_t receiver,
                                  const int32_t *args, uint16_t argc) {
  (void)vm;
  (void)receiver;
  if (argc < 1 || args == NULL) {
    printf("program stdout: 0\n");
    fflush(stdout);
    return 0;
  }
  int32_t v = args[0];
  printf("program stdout: %d\n", (int)v);
  fflush(stdout);
  return 0;
}

static int32_t native_println_string(jmevm_vm *vm, int32_t receiver,
                                     const int32_t *args, uint16_t argc) {
  (void)vm;
  (void)receiver;
  if (argc < 1 || args == NULL) {
    printf("program stdout: \n");
    fflush(stdout);
    return 0;
  }

  int32_t string_ref = args[0];
  if (string_ref == 0) {
    printf("program stdout: null\n");
  } else {
    int32_t barry_ref = jmevm_object_get_field(string_ref, 0);
    int32_t len = jmevm_object_get_field(string_ref, 1);
    printf("program stdout: ");
    for (int32_t i = 0; i < len; i++) {
      putchar(jmevm_array_load_byte(barry_ref, i));
    }
    putchar('\n');
  }
  fflush(stdout);
  return 0;
}

void jmevm_runtime_init_native(void) {
  if (g_native_inited) {
    return;
  }

  /* Native bridge for: System.out.println(int) */
  (void)jmevm_native_register("java/io/PrintStream", "println", "(I)V",
                              native_println_int);

  (void)jmevm_native_register("java/io/PrintStream", "println",
                              "(Ljava/lang/String;)V", native_println_string);

  g_native_inited = 1;
}

int32_t jmevm_runtime_stub_system_out(void) {
  /* We only need a non-zero token so invokevirtual can pop a receiver. */
  return JMEVM_REF_PRINTSTREAM;
}

int32_t jmevm_runtime_stub_object(void) {
  /* Placeholder object identity (no heap yet). */
  return 0;
}

int32_t jmevm_runtime_stub_system(void) {
  /* java/lang/System is treated as static-only in this minimal VM. */
  return 0;
}
