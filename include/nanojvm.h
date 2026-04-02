#ifndef JVM_JVM_H
#define JVM_JVM_H

#include <stddef.h>
#include <stdint.h>

typedef struct jvm_vm jvm_vm;

typedef struct jvm_classfile jvm_classfile;

typedef struct jvm_main_method {
  const uint8_t *code;
  size_t code_len;
  uint16_t max_stack;
  uint16_t max_locals;
} jvm_main_method;

jvm_vm *jvm_vm_create(void);
void jvm_vm_destroy(jvm_vm *vm);

void jvm_vm_register_class(jvm_vm *vm, jvm_classfile *cf);

int jvm_vm_run(jvm_vm *vm, const jvm_classfile *cf, const uint8_t *code,
               size_t code_len, uint16_t max_locals, uint16_t max_stack);

int jvm_vm_run_main(jvm_vm *vm, const char *main_class_name);

int jvm_vm_get_local_i32(const jvm_vm *vm, uint16_t index, int32_t *out);

/* Minimal .class parsing/execution (only enough for extracting a "main"
 * method). */
jvm_classfile *jvm_classfile_load_from_buffer(const uint8_t *buf, size_t len);
void jvm_classfile_destroy(jvm_classfile *cf);

/* Returns 0 on success, negative on failure. */
int jvm_classfile_extract_main(const jvm_classfile *cf, jvm_main_method *out);

/* Parses the class and executes the extracted main bytecode using the existing
 * interpreter. */
int jvm_classfile_execute_main(jvm_vm *vm, const uint8_t *buf, size_t len);

#endif
