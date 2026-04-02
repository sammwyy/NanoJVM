#ifndef JMEVM_JMEVM_H
#define JMEVM_JMEVM_H

#include <stddef.h>
#include <stdint.h>

typedef struct jmevm_vm jmevm_vm;

jmevm_vm *jmevm_vm_create(void);
void jmevm_vm_destroy(jmevm_vm *vm);

int jmevm_vm_run(
    jmevm_vm *vm,
    const uint8_t *code,
    size_t code_len,
    uint16_t max_locals,
    uint16_t max_stack
);

int jmevm_vm_get_local_i32(const jmevm_vm *vm, uint16_t index, int32_t *out);

#endif
