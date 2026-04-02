#ifndef JMEVM_JMEVM_H
#define JMEVM_JMEVM_H

#include <stddef.h>
#include <stdint.h>

typedef struct jmevm_vm jmevm_vm;

typedef struct jmevm_classfile jmevm_classfile;

typedef struct jmevm_main_method {
  const uint8_t *code;
  size_t code_len;
  uint16_t max_stack;
  uint16_t max_locals;
} jmevm_main_method;

jmevm_vm *jmevm_vm_create(void);
void jmevm_vm_destroy(jmevm_vm *vm);

void jmevm_vm_register_class(jmevm_vm *vm, jmevm_classfile *cf);

int jmevm_vm_run(jmevm_vm *vm, const jmevm_classfile *cf, const uint8_t *code,
                 size_t code_len, uint16_t max_locals, uint16_t max_stack);

int jmevm_vm_run_main(jmevm_vm *vm, const char *main_class_name);

int jmevm_vm_get_local_i32(const jmevm_vm *vm, uint16_t index, int32_t *out);

/* Minimal .class parsing/execution (only enough for extracting a "main"
 * method). */
jmevm_classfile *jmevm_classfile_load_from_buffer(const uint8_t *buf,
                                                  size_t len);
void jmevm_classfile_destroy(jmevm_classfile *cf);

/* Returns 0 on success, negative on failure. */
int jmevm_classfile_extract_main(const jmevm_classfile *cf,
                                 jmevm_main_method *out);

/* Parses the class and executes the extracted main bytecode using the existing
 * interpreter. */
int jmevm_classfile_execute_main(jmevm_vm *vm, const uint8_t *buf, size_t len);

#endif
