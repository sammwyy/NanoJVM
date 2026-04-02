#ifndef JMEVM_CORE_VM_H
#define JMEVM_CORE_VM_H

#include <stddef.h>
#include <stdint.h>

struct jmevm_classfile;

#define JMEVM_MAX_LOCALS 64
#define JMEVM_MAX_STACK 64
#define JMEVM_MAX_CALLSTACK 16
#define JMEVM_MAX_CLASSES 32

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

typedef struct jmevm_vm {
  const struct jmevm_classfile *cf;
  jmevm_frame frames[JMEVM_MAX_CALLSTACK];
  uint16_t frame_top; /* number of active frames */
  const struct jmevm_classfile *classes[JMEVM_MAX_CLASSES];
  uint16_t class_count;

  int32_t exception_obj; /* 0 if no exception is pending */
} jmevm_vm;

#endif
