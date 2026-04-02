#ifndef JMEVM_CORE_RUNTIME_H
#define JMEVM_CORE_RUNTIME_H

#include "jmevm.h"

#include <stddef.h>
#include <stdint.h>

typedef int32_t (*jmevm_native_fn)(
    jmevm_vm *vm,
    int32_t receiver,
    const int32_t *args,
    uint16_t argc);

struct jmevm_native_method {
    const char *class_name; /* e.g. "java/io/PrintStream" */
    const char *method_name; /* e.g. "println" */
    const char *descriptor; /* e.g. "(I)V" */
    jmevm_native_fn fn;
};

/* Registers a new native method in the current process. */
int jmevm_native_register(
    const char *class_name,
    const char *method_name,
    const char *descriptor,
    jmevm_native_fn fn);

/* Initializes the minimal native set required by this VM. */
void jmevm_runtime_init_native(void);

/* Minimal stubs: provides an implicit receiver token for System.out. */
int32_t jmevm_runtime_stub_system_out(void);

/* Minimal stubs for java/lang/Object and java/lang/System.
 * These are only present to satisfy the "runtime stub" concept; the VM does not
 * implement real heap-backed objects yet. */
int32_t jmevm_runtime_stub_object(void);
int32_t jmevm_runtime_stub_system(void);

/* Lookup based on raw UTF-8 bytes from the constant pool. */
const struct jmevm_native_method *jmevm_native_lookup_utf8(
    const uint8_t *class_bytes,
    size_t class_len,
    const uint8_t *method_bytes,
    size_t method_len,
    const uint8_t *desc_bytes,
    size_t desc_len);

#endif
