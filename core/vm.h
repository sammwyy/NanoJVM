#ifndef JMEVM_CORE_VM_H
#define JMEVM_CORE_VM_H

#include <stddef.h>
#include <stdint.h>

#define JMEVM_MAX_LOCALS 64
#define JMEVM_MAX_STACK 64

typedef struct jmevm_frame {
  const struct jmevm_classfile *cf;
  uint32_t pc;
  const uint8_t *code;
  size_t code_len;
  int32_t locals[JMEVM_MAX_LOCALS];
  int32_t stack[JMEVM_MAX_STACK];
  uint16_t sp;
  uint16_t max_locals;
  uint16_t max_stack;
} jmevm_frame;

#endif
