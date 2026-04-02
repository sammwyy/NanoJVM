#ifndef JVM_CORE_VM_H
#define JVM_CORE_VM_H

#include <stddef.h>
#include <stdint.h>

struct jvm_classfile;

#define JVM_MAX_LOCALS 64
#define JVM_MAX_STACK 64
#define JVM_MAX_CALLSTACK 16
#define JVM_MAX_CLASSES 32

typedef struct jvm_frame {
  const struct jvm_classfile *cf;
  uint32_t pc;
  const uint8_t *code;
  size_t code_len;
  int32_t locals[JVM_MAX_LOCALS];
  int32_t stack[JVM_MAX_STACK];
  uint16_t sp;
  uint16_t max_locals;
  uint16_t max_stack;
} jvm_frame;

typedef struct jvm_vm {
  const struct jvm_classfile *cf;
  jvm_frame frames[JVM_MAX_CALLSTACK];
  uint16_t frame_top; /* number of active frames */
  const struct jvm_classfile *classes[JVM_MAX_CLASSES];
  uint16_t class_count;

  int32_t exception_obj; /* 0 if no exception is pending */
} jvm_vm;

#endif
