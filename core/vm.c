#include "core/vm.h"
#include "core/bytecode.h"
#include "core/classfile.h"
#include "core/heap.h"
#include "core/runtime.h"
#include "jmevm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JMEVM_MAX_CALLSTACK 16

#define JMEVM_MAX_CLASSES 32

struct jmevm_vm;
static const struct jmevm_classfile *jmevm_vm_find_class(struct jmevm_vm *vm,
                                                         const char *name);

struct jmevm_vm {
  const struct jmevm_classfile *cf;
  jmevm_frame frames[JMEVM_MAX_CALLSTACK];
  uint16_t frame_top; /* number of active frames */
  const struct jmevm_classfile *classes[JMEVM_MAX_CLASSES];
  uint16_t class_count;

  int32_t exception_obj; /* 0 if no exception is pending */
};

#define JMEVM_OK 0
#define JMEVM_ERR_UNKNOWN_OPCODE -1
#define JMEVM_ERR_STACK_OVERFLOW -2
#define JMEVM_ERR_STACK_UNDERFLOW -3
#define JMEVM_ERR_TRUNCATED -4
#define JMEVM_ERR_LOCAL_BOUNDS -5
#define JMEVM_ERR_BAD_ARGS -6
#define JMEVM_ERR_STACK_NOT_EMPTY -7
#define JMEVM_ERR_CALLSTACK_OVERFLOW -8

static int frame_push(jmevm_frame *f, int32_t v) {
  if (f->sp >= f->max_stack) {
    return JMEVM_ERR_STACK_OVERFLOW;
  }
  f->stack[f->sp++] = v;
  return JMEVM_OK;
}

static int frame_pop(jmevm_frame *f, int32_t *out) {
  if (f->sp == 0) {
    return JMEVM_ERR_STACK_UNDERFLOW;
  }
  *out = f->stack[--f->sp];
  return JMEVM_OK;
}

/* Constant-pool tags used by invokestatic resolution. */
#define CONSTANT_Methodref 10
#define CONSTANT_InterfaceMethodref 11
#define CONSTANT_Fieldref 9
#define CONSTANT_Integer 3
#define CONSTANT_String 8
#define CONSTANT_Utf8 1

static int parse_descriptor_minimal_i32_void(const uint8_t *desc,
                                             size_t desc_len,
                                             uint16_t *out_param_count,
                                             int *out_ret_is_int) {
  if (desc == NULL || out_param_count == NULL || out_ret_is_int == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (desc_len < 3) {
    return JMEVM_ERR_BAD_ARGS;
  }

  if (desc[0] != '(') {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t param_count = 0;
  size_t pos = 1;
  while (pos < desc_len && desc[pos] != ')') {
    if (desc[pos] == 'I') {
      param_count++;
      pos++;
    } else if (desc[pos] == 'L') {
      /* Simplified: assume L...; is a reference, push as i32. */
      while (pos < desc_len && desc[pos] != ';') {
        pos++;
      }
      if (pos >= desc_len) {
        return JMEVM_ERR_BAD_ARGS;
      }
      pos++; /* consume ';' */
      param_count++;
    } else {
      /* Only int and String parameters are supported in this minimal VM. */
      return JMEVM_ERR_BAD_ARGS;
    }
    if (param_count > JMEVM_MAX_LOCALS) {
      return JMEVM_ERR_BAD_ARGS;
    }
  }
  if (pos >= desc_len || desc[pos] != ')') {
    return JMEVM_ERR_BAD_ARGS;
  }
  pos++; /* consume ')' */

  if (pos >= desc_len || (desc[pos] != 'I' && desc[pos] != 'V')) {
    return JMEVM_ERR_BAD_ARGS;
  }
  *out_ret_is_int = (desc[pos] == 'I');
  pos++;

  if (pos != desc_len) {
    return JMEVM_ERR_BAD_ARGS;
  }

  *out_param_count = param_count;
  return JMEVM_OK;
}

static int is_system_out_fieldref(const struct jmevm_classfile *cf,
                                  uint16_t fieldref_cp_index) {
  if (cf == NULL || fieldref_cp_index == 0 ||
      fieldref_cp_index >= cf->cp_count) {
    return 0;
  }
  if (cf->cp_tag == NULL || cf->cp_fieldref_class_index == NULL ||
      cf->cp_fieldref_nat_index == NULL) {
    return 0;
  }
  if (cf->cp_tag[fieldref_cp_index] != CONSTANT_Fieldref) {
    return 0;
  }

  uint16_t class_index = cf->cp_fieldref_class_index[fieldref_cp_index];
  uint16_t nat_index = cf->cp_fieldref_nat_index[fieldref_cp_index];
  if (class_index == 0 || nat_index == 0) {
    return 0;
  }

  uint16_t class_name_cp_index =
      cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;
  uint16_t field_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t field_desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (class_name_cp_index == 0 || field_name_cp_index == 0 ||
      field_desc_cp_index == 0) {
    return 0;
  }
  if (cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL || cf->buf == NULL) {
    return 0;
  }

  const char sys_class[] = "java/lang/System";
  const char out_name[] = "out";
  const char out_desc[] = "Ljava/io/PrintStream;";

  if (cf->cp_tag[class_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[field_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[field_desc_cp_index] != CONSTANT_Utf8) {
    return 0;
  }

  const uint8_t *class_bytes = cf->buf + cf->cp_utf8_off[class_name_cp_index];
  size_t class_len = (size_t)cf->cp_utf8_len[class_name_cp_index];
  const uint8_t *name_bytes = cf->buf + cf->cp_utf8_off[field_name_cp_index];
  size_t name_len = (size_t)cf->cp_utf8_len[field_name_cp_index];
  const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[field_desc_cp_index];
  size_t desc_len = (size_t)cf->cp_utf8_len[field_desc_cp_index];

  if (class_len != strlen(sys_class) ||
      memcmp(class_bytes, sys_class, class_len) != 0) {
    return 0;
  }
  if (name_len != strlen(out_name) ||
      memcmp(name_bytes, out_name, name_len) != 0) {
    return 0;
  }
  if (desc_len != strlen(out_desc) ||
      memcmp(desc_bytes, out_desc, desc_len) != 0) {
    return 0;
  }
  return 1;
}

static int
resolve_native_from_methodref(const struct jmevm_classfile *cf,
                              uint16_t methodref_cp_index,
                              const struct jmevm_native_method **out_native,
                              uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_native == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }
  *out_native = NULL;
  *out_arg_count = 0;
  *out_ret_is_int = 0;

  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (cf->cp_tag == NULL || cf->cp_methodref_class_index == NULL ||
      cf->cp_methodref_nat_index == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint8_t tag = cf->cp_tag[methodref_cp_index];
  if (tag != CONSTANT_Methodref && tag != CONSTANT_InterfaceMethodref) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t class_index = cf->cp_methodref_class_index
                             ? cf->cp_methodref_class_index[methodref_cp_index]
                             : 0;
  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (class_index == 0 || nat_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t class_name_cp_index =
      cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;
  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (class_name_cp_index == 0 || method_name_cp_index == 0 ||
      desc_cp_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  if (cf->cp_tag[class_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[method_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[desc_cp_index] != CONSTANT_Utf8) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL || cf->buf == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  const uint8_t *class_bytes = cf->buf + cf->cp_utf8_off[class_name_cp_index];
  size_t class_len = (size_t)cf->cp_utf8_len[class_name_cp_index];
  const uint8_t *method_bytes = cf->buf + cf->cp_utf8_off[method_name_cp_index];
  size_t method_len = (size_t)cf->cp_utf8_len[method_name_cp_index];
  const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_cp_index];
  size_t desc_len = (size_t)cf->cp_utf8_len[desc_cp_index];

  const struct jmevm_native_method *nm = jmevm_native_lookup_utf8(
      class_bytes, class_len, method_bytes, method_len, desc_bytes, desc_len);
  if (nm == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count,
                                             &ret_is_int);
  if (rc != JMEVM_OK) {
    return rc;
  }

  *out_native = nm;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JMEVM_OK;
}

static int resolve_invokestatic(const struct jmevm_classfile *cf,
                                uint16_t methodref_cp_index,
                                const struct jmevm_method **out_method,
                                uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_method == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JMEVM_ERR_BAD_ARGS;
  }
  uint8_t tag = cf->cp_tag ? cf->cp_tag[methodref_cp_index] : 0;
  if (tag != CONSTANT_Methodref && tag != CONSTANT_InterfaceMethodref) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t class_index = cf->cp_methodref_class_index
                             ? cf->cp_methodref_class_index[methodref_cp_index]
                             : 0;
  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (class_index == 0 || nat_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (method_name_cp_index == 0 || desc_cp_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t class_name_cp_index =
      cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;
  if (cf->this_class_name_cp_index != 0 &&
      class_name_cp_index != cf->this_class_name_cp_index) {
    /* For now, only resolve within the same class. */
    return JMEVM_ERR_BAD_ARGS;
  }

  const struct jmevm_method *m =
      jmevm_classfile_lookup_method(cf, method_name_cp_index, desc_cp_index);
  if (m == NULL || m->code == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  if (desc_cp_index >= cf->cp_count || cf->cp_utf8_off == NULL ||
      cf->cp_utf8_len == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_cp_index];
  size_t desc_len = (size_t)cf->cp_utf8_len[desc_cp_index];

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count,
                                             &ret_is_int);
  if (rc != JMEVM_OK) {
    return rc;
  }

  if (arg_count > m->max_locals) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (m->max_locals > JMEVM_MAX_LOCALS || m->max_stack > JMEVM_MAX_STACK) {
    return JMEVM_ERR_BAD_ARGS;
  }

  *out_method = m;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JMEVM_OK;
}

static int resolve_invokevirtual(const struct jmevm_classfile *cf,
                                 uint16_t methodref_cp_index,
                                 const struct jmevm_classfile *obj_cf,
                                 const struct jmevm_method **out_method,
                                 uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_method == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL || obj_cf == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (nat_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (method_name_cp_index == 0 || desc_cp_index == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  char *method_name = jmevm_classfile_get_utf8_copy(cf, method_name_cp_index);
  char *method_desc = jmevm_classfile_get_utf8_copy(cf, desc_cp_index);

  const struct jmevm_method *m =
      jmevm_classfile_resolve_method(obj_cf, method_name, method_desc);

  if (m == NULL || m->code == NULL) {
    free(method_name);
    free(method_desc);
    return JMEVM_ERR_BAD_ARGS;
  }

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void((const uint8_t *)method_desc,
                                             strlen(method_desc), &arg_count,
                                             &ret_is_int);

  free(method_name);
  free(method_desc);

  if (rc != JMEVM_OK) {
    return rc;
  }

  if (m->max_locals > JMEVM_MAX_LOCALS || m->max_stack > JMEVM_MAX_STACK) {
    return JMEVM_ERR_BAD_ARGS;
  }

  *out_method = m;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JMEVM_OK;
}

static int class_is_assignable_to(struct jmevm_vm *vm,
                                  const struct jmevm_classfile *child,
                                  const char *parent_name) {
  while (child != NULL) {
    if (jmevm_classfile_utf8_equals(child, child->this_class_name_cp_index,
                                    parent_name)) {
      return 1;
    }
    if (child->super_class_name_cp_index == 0)
      break;
    char *super_name =
        jmevm_classfile_get_utf8_copy(child, child->super_class_name_cp_index);
    const struct jmevm_classfile *super_cf =
        jmevm_vm_find_class(vm, super_name);
    free(super_name);
    child = super_cf;
  }
  return 0;
}

static int vm_handle_exception(struct jmevm_vm *vm) {
  if (vm->exception_obj == 0) {
    return JMEVM_OK;
  }

  int32_t ex_obj = vm->exception_obj;
  const struct jmevm_classfile *ex_cf = jmevm_heap_get_classfile(ex_obj);
  if (ex_cf == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  while (vm->frame_top > 0) {
    jmevm_frame *f = &vm->frames[vm->frame_top - 1];
    const struct jmevm_classfile *cf = f->cf;

    /* Find the method that this frame belongs to.
     * In this minimal VM, frames don't store the method pointer directly,
     * but we can find it by matching the code pointer. */
    const struct jmevm_method *m = NULL;
    for (uint16_t i = 0; i < cf->methods_count; i++) {
      if (cf->methods[i].code == f->code) {
        m = &cf->methods[i];
        break;
      }
    }

    if (m != NULL) {
      for (uint16_t i = 0; i < m->exception_table_length; i++) {
        struct jmevm_exception_handler *h = &m->exception_table[i];
        /* PC is the offset of the NEXT instruction, but h->end_pc is exclusive.
         * The instruction that threw the exception is at f->pc - 1.
         * HOWEVER, for implicit exceptions or calls, f->pc might already point
         * to the next instruction.
         * JVM spec says: start_pc <= pc < end_pc.
         * Let's use f->pc - 1 for the check if it was an instruction throw.
         * Actually, let's just use the current f->pc value adjusted by the
         * opcode length if needed. Or more simply, since we just incremented
         * f->pc, the throwing instruction was at f->pc - 1 (for 1-byte
         * opcodes). For calls, f->pc points to the instruction AFTER the
         * invoke. */
        uint32_t throwing_pc = f->pc - 1;

        if (throwing_pc >= h->start_pc && throwing_pc < h->end_pc) {
          int match = 0;
          if (h->catch_type == 0) {
            match = 1;
          } else {
            /* Match by exact class name. */
            uint16_t class_entry = h->catch_type;
            uint16_t catch_name_idx = cf->cp_class_name_index[class_entry];
            char *catch_name =
                jmevm_classfile_get_utf8_copy(cf, catch_name_idx);
            if (catch_name) {
              if (class_is_assignable_to(vm, ex_cf, catch_name)) {
                match = 1;
              }
              free(catch_name);
            }
          }

          if (match) {
            f->pc = h->handler_pc;
            f->sp = 0;
            frame_push(f, ex_obj);
            vm->exception_obj = 0;
            return JMEVM_OK;
          }
        }
      }
    }

    /* Not found in this frame, unwind. */
    vm->frame_top--;
  }

  fprintf(stderr, "Uncaught exception: %p\n", (void *)(uintptr_t)ex_obj);
  return -1; /* Terminate VM */
}

static void jmevm_vm_throw_implicit(struct jmevm_vm *vm,
                                    const char *class_name) {
  const struct jmevm_classfile *ex_cf = jmevm_vm_find_class(vm, class_name);
  if (ex_cf == NULL) {
    /* If the exception class is not loaded, just use a generic error.
     * In a real VM we'd load it. Here we assume typical classes are loaded. */
    fprintf(stderr, "FATAL: Could not find exception class %s\n", class_name);
    exit(1);
  }
  vm->exception_obj = jmevm_heap_alloc(ex_cf);
}

static int vm_exec(struct jmevm_vm *vm) {
  int rc;

  for (;;) {
    if (vm->frame_top == 0) {
      return JMEVM_OK;
    }

    if (vm->exception_obj != 0) {
      rc = vm_handle_exception(vm);
      if (rc != JMEVM_OK) {
        return rc;
      }
      continue;
    }

    jmevm_frame *f = &vm->frames[vm->frame_top - 1];
    if (f->pc >= f->code_len) {
      return JMEVM_ERR_TRUNCATED;
    }

    const struct jmevm_classfile *cf = f->cf;
    uint8_t op = f->code[f->pc++];

    switch (op) {
    case JMEVM_OP_ICONST_M1:
      rc = frame_push(f, -1);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_0:
      rc = frame_push(f, 0);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_1:
      rc = frame_push(f, 1);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_2:
      rc = frame_push(f, 2);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_3:
      rc = frame_push(f, 3);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_4:
      rc = frame_push(f, 4);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ICONST_5:
      rc = frame_push(f, 5);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;

    case JMEVM_OP_BIPUSH: {
      if (f->pc >= f->code_len)
        return JMEVM_ERR_TRUNCATED;
      int8_t v = (int8_t)f->code[f->pc++];
      rc = frame_push(f, (int32_t)v);
      if (rc != JMEVM_OK)
        return rc;
      break;
    }
    case JMEVM_OP_SIPUSH: {
      if (f->pc + 1 >= f->code_len)
        return JMEVM_ERR_TRUNCATED;
      int16_t v = ((int16_t)f->code[f->pc] << 8) | (int16_t)f->code[f->pc + 1];
      f->pc += 2;
      rc = frame_push(f, (int32_t)v);
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_ALOAD:
    case JMEVM_OP_ILOAD: {
      if (f->pc >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint8_t idx = f->code[f->pc++];
      if (idx >= f->max_locals) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[idx]);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    }
    case JMEVM_OP_ALOAD_0:
    case JMEVM_OP_ILOAD_0:
      if (f->max_locals < 1) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[0]);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ALOAD_1:
    case JMEVM_OP_ILOAD_1:
      if (f->max_locals < 2) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[1]);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ALOAD_2:
    case JMEVM_OP_ILOAD_2:
      if (f->max_locals < 3) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[2]);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    case JMEVM_OP_ALOAD_3:
    case JMEVM_OP_ILOAD_3:
      if (f->max_locals < 4) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[3]);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;

    case JMEVM_OP_DUP: {
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_push(f, v);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_push(f, v);
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_ASTORE:
    case JMEVM_OP_ISTORE: {
      if (f->pc >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint8_t idx = f->code[f->pc++];
      if (idx >= f->max_locals) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK) {
        return rc;
      }
      f->locals[idx] = v;
      break;
    }
    case JMEVM_OP_ASTORE_0:
    case JMEVM_OP_ISTORE_0: {
      if (f->max_locals < 1) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK) {
        return rc;
      }
      f->locals[0] = v;
      break;
    }
    case JMEVM_OP_ASTORE_1:
    case JMEVM_OP_ISTORE_1: {
      if (f->max_locals < 2) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK) {
        return rc;
      }
      f->locals[1] = v;
      break;
    }
    case JMEVM_OP_ASTORE_2:
    case JMEVM_OP_ISTORE_2: {
      if (f->max_locals < 3) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK) {
        return rc;
      }
      f->locals[2] = v;
      break;
    }
    case JMEVM_OP_ASTORE_3:
    case JMEVM_OP_ISTORE_3: {
      if (f->max_locals < 4) {
        return JMEVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JMEVM_OK) {
        return rc;
      }
      f->locals[3] = v;
      break;
    }

    case JMEVM_OP_IADD: {
      int32_t a, b;
      rc = frame_pop(f, &b);
      if (rc != JMEVM_OK) {
        return rc;
      }
      rc = frame_pop(f, &a);
      if (rc != JMEVM_OK) {
        return rc;
      }
      rc = frame_push(f, a + b);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    }
    case JMEVM_OP_ISUB: {
      int32_t a, b;
      rc = frame_pop(f, &b);
      if (rc != JMEVM_OK) {
        return rc;
      }
      rc = frame_pop(f, &a);
      if (rc != JMEVM_OK) {
        return rc;
      }
      rc = frame_push(f, a - b);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    }
    case JMEVM_OP_INVOKESTATIC: {
      if (f->pc + 1 >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint16_t methodref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jmevm_native_method *nm = NULL;
      uint16_t arg_count = 0;
      int ret_is_int = 0;

      rc = resolve_native_from_methodref(cf, methodref_cp_index, &nm,
                                         &arg_count, &ret_is_int);
      if (rc == JMEVM_OK && nm != NULL) {
        /* Native static method: no receiver on the operand stack. */
        if (arg_count > f->sp) {
          return JMEVM_ERR_STACK_UNDERFLOW;
        }
        int32_t args[JMEVM_MAX_LOCALS];
        for (int i = (int)arg_count - 1; i >= 0; i--) {
          rc = frame_pop(f, &args[(size_t)i]);
          if (rc != JMEVM_OK) {
            return rc;
          }
        }
        int32_t ret = nm->fn(vm, 0, args, arg_count);
        if (ret_is_int) {
          rc = frame_push(f, ret);
          if (rc != JMEVM_OK) {
            return rc;
          }
        }
        continue;
      }

      const struct jmevm_method *m = NULL;
      rc = resolve_invokestatic(cf, methodref_cp_index, &m, &arg_count,
                                &ret_is_int);
      if (rc != JMEVM_OK) {
        return rc;
      }
      (void)ret_is_int;

      if (arg_count > f->sp) {
        return JMEVM_ERR_STACK_UNDERFLOW;
      }
      if (vm->frame_top >= JMEVM_MAX_CALLSTACK) {
        return JMEVM_ERR_CALLSTACK_OVERFLOW;
      }

      int32_t args[JMEVM_MAX_LOCALS];
      for (int i = (int)arg_count - 1; i >= 0; i--) {
        rc = frame_pop(f, &args[(size_t)i]);
        if (rc != JMEVM_OK) {
          return rc;
        }
      }

      jmevm_frame *callee = &vm->frames[vm->frame_top++];
      if (m->max_locals > JMEVM_MAX_LOCALS || m->max_stack > JMEVM_MAX_STACK) {
        return JMEVM_ERR_BAD_ARGS;
      }

      callee->cf = cf; // Static call: same class as caller for now or resolved
                       // by Methodref
      callee->pc = 0;
      callee->code = m->code;
      callee->code_len = m->code_len;
      callee->sp = 0;
      callee->max_locals = m->max_locals;
      callee->max_stack = m->max_stack;

      for (uint16_t li = 0; li < callee->max_locals; li++) {
        callee->locals[li] = 0;
      }
      for (uint16_t li = 0; li < arg_count; li++) {
        callee->locals[li] = args[li];
      }

      /* Execute callee next. */
      continue;
    }

    case JMEVM_OP_GETSTATIC: {
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      /* Minimal support: only java/lang/System.out. */
      if (is_system_out_fieldref(cf, fieldref_cp_index)) {
        rc = frame_push(f, jmevm_runtime_stub_system_out());
        if (rc != JMEVM_OK) {
          return rc;
        }
        continue;
      }
      return JMEVM_ERR_BAD_ARGS;
    }

    case JMEVM_OP_INVOKESPECIAL:
    case JMEVM_OP_INVOKEVIRTUAL: {
      if (f->pc + 1 >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint16_t methodref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jmevm_native_method *nm = NULL;
      uint16_t arg_count = 0;
      int ret_is_int = 0;

      // Try native first
      rc = resolve_native_from_methodref(cf, methodref_cp_index, &nm,
                                         &arg_count, &ret_is_int);
      if (rc == JMEVM_OK && nm != NULL) {
        /* Virtual methods: stack has receiver + descriptor params. */
        if ((uint16_t)(arg_count + 1) > f->sp) {
          return JMEVM_ERR_STACK_UNDERFLOW;
        }

        int32_t args[JMEVM_MAX_LOCALS];
        for (int i = (int)arg_count - 1; i >= 0; i--) {
          rc = frame_pop(f, &args[(size_t)i]);
          if (rc != JMEVM_OK) {
            return rc;
          }
        }
        int32_t receiver = 0;
        rc = frame_pop(f, &receiver);
        if (rc != JMEVM_OK) {
          return rc;
        }

        if (receiver == 0) {
          jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
          continue;
        }

        int32_t ret = nm->fn(vm, receiver, args, arg_count);
        if (ret_is_int) {
          rc = frame_push(f, ret);
          if (rc != JMEVM_OK) {
            return rc;
          }
        }
        continue;
      }

      // If not native, resolve as Java method
      // We need to peek at the receiver to get its class
      // First, get arg_count from descriptor
      uint16_t nat_index = cf->cp_methodref_nat_index[methodref_cp_index];
      uint16_t desc_idx = cf->cp_nat_desc_index[nat_index];
      const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_idx];
      size_t desc_len = (size_t)cf->cp_utf8_len[desc_idx];
      rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count,
                                             &ret_is_int);
      if (rc != JMEVM_OK) {
        return rc;
      }

      if (f->sp < (uint16_t)(arg_count + 1)) {
        return JMEVM_ERR_STACK_UNDERFLOW;
      }

      int32_t receiver = f->stack[f->sp - arg_count - 1];
      if (receiver == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      const struct jmevm_classfile *target_class = NULL;
      uint8_t op_prev = f->code[f->pc - 3];

      if (op_prev == JMEVM_OP_INVOKESPECIAL) {
        uint16_t class_cp_idx =
            cf->cp_methodref_class_index[methodref_cp_index];
        uint16_t class_name_idx = cf->cp_class_name_index[class_cp_idx];
        char *target_class_name =
            jmevm_classfile_get_utf8_copy(cf, class_name_idx);
        target_class = jmevm_vm_find_class(vm, target_class_name);
        free(target_class_name);
      } else {
        target_class = jmevm_heap_get_classfile(receiver);
      }

      if (target_class == NULL && op_prev != JMEVM_OP_INVOKESPECIAL) {
        // Fallback for INVOKEVIRTUAL stubs (like PrintStream)
        target_class = cf;
      }

      const struct jmevm_method *m = NULL;
      rc = resolve_invokevirtual(cf, methodref_cp_index, target_class, &m,
                                 &arg_count, &ret_is_int);
      if (rc != JMEVM_OK) {
        // Special case: if it's <init> and not found, maybe it's
        // java/lang/Object.<init> which we can safely ignore in this minimal
        // VM.
        char *mname =
            jmevm_classfile_get_utf8_copy(cf, cf->cp_nat_name_index[nat_index]);
        if (strcmp(mname, "<init>") == 0) {
          free(mname);
          // Pop args and receiver and continue
          for (int i = 0; i < (int)arg_count + 1; i++) {
            int32_t dummy;
            frame_pop(f, &dummy);
          }
          continue;
        }
        free(mname);
        return rc;
      }

      if (vm->frame_top >= JMEVM_MAX_CALLSTACK) {
        return JMEVM_ERR_CALLSTACK_OVERFLOW;
      }

      int32_t args[JMEVM_MAX_LOCALS];
      for (int i = (int)arg_count - 1; i >= 0; i--) {
        rc = frame_pop(f, &args[(size_t)i]);
        if (rc != JMEVM_OK) {
          return rc;
        }
      }
      int32_t discard_receiver;
      rc = frame_pop(f, &discard_receiver);
      if (rc != JMEVM_OK) {
        return rc;
      }

      jmevm_frame *callee = &vm->frames[vm->frame_top++];
      callee->cf = target_class;
      callee->pc = 0;
      callee->code = m->code;
      callee->code_len = m->code_len;
      callee->sp = 0;
      callee->max_locals = m->max_locals;
      callee->max_stack = m->max_stack;

      for (uint16_t li = 0; li < callee->max_locals; li++) {
        callee->locals[li] = 0;
      }
      callee->locals[0] = receiver;
      for (uint16_t li = 0; li < arg_count; li++) {
        callee->locals[li + 1] = args[li];
      }

      continue;
    }

    case JMEVM_OP_IRETURN: {
      if (f->sp != 1) {
        return JMEVM_ERR_STACK_NOT_EMPTY;
      }
      int32_t ret = 0;
      rc = frame_pop(f, &ret);
      if (rc != JMEVM_OK) {
        return rc;
      }

      vm->frame_top--;
      if (vm->frame_top == 0) {
        /* Top-level ireturn isn't currently exposed; treat as success. */
        return JMEVM_OK;
      }

      jmevm_frame *caller = &vm->frames[vm->frame_top - 1];
      rc = frame_push(caller, ret);
      if (rc != JMEVM_OK) {
        return rc;
      }
      continue;
    }

    case JMEVM_OP_RETURN:
      if (f->sp != 0) {
        return JMEVM_ERR_STACK_NOT_EMPTY;
      }
      vm->frame_top--;
      if (vm->frame_top == 0) {
        return JMEVM_OK;
      }
      continue;

    case JMEVM_OP_NEW: {
      if (f->pc + 1 >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      // uint16_t class_index = ((uint16_t)f->code[f->pc] << 8) |
      // (uint16_t)f->code[f->pc + 1];
      uint16_t class_cp_idx =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jmevm_classfile *target_cf = NULL;
      uint16_t class_name_idx = cf->cp_class_name_index[class_cp_idx];
      char *class_name = jmevm_classfile_get_utf8_copy(cf, class_name_idx);

      target_cf = jmevm_vm_find_class(vm, class_name);
      free(class_name);

      if (target_cf == NULL) {
        return JMEVM_ERR_BAD_ARGS;
      }

      int32_t obj_ref = jmevm_heap_alloc(target_cf);
      if (obj_ref == 0) {
        return JMEVM_ERR_UNKNOWN_OPCODE; // Treat as OOM or general error
      }
      rc = frame_push(f, obj_ref);
      if (rc != JMEVM_OK) {
        return rc;
      }
      break;
    }

    case JMEVM_OP_PUTFIELD: {
      if (f->pc + 1 >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      int32_t val, obj_ref;
      rc = frame_pop(f, &val);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &obj_ref);
      if (rc != JMEVM_OK)
        return rc;

      if (obj_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      // Resolve field name and descriptor from Fieldref in current CP
      uint16_t nat_idx = cf->cp_fieldref_nat_index[fieldref_cp_index];
      uint16_t name_idx = cf->cp_nat_name_index[nat_idx];
      uint16_t desc_idx = cf->cp_nat_desc_index[nat_idx];

      char *name = jmevm_classfile_get_utf8_copy(cf, name_idx);
      char *desc = jmevm_classfile_get_utf8_copy(cf, desc_idx);

      // Resolve in target's class
      const struct jmevm_classfile *target_cf =
          jmevm_heap_get_classfile(obj_ref);
      int field_index = jmevm_classfile_resolve_field(target_cf, name, desc);
      free(name);
      free(desc);

      if (field_index < 0)
        return JMEVM_ERR_BAD_ARGS;

      jmevm_object_put_field(obj_ref, (uint16_t)field_index, val);
      break;
    }

    case JMEVM_OP_LDC: {
      if (f->pc >= f->code_len)
        return JMEVM_ERR_TRUNCATED;
      uint8_t cp_idx = f->code[f->pc++];
      uint8_t tag = cf->cp_tag[cp_idx];
      if (tag == CONSTANT_Integer) {
        rc = frame_push(f, cf->cp_integer[cp_idx]);
        if (rc != JMEVM_OK)
          return rc;
      } else if (tag == CONSTANT_String) {
        uint16_t utf8_idx = cf->cp_string_index[cp_idx];
        char *s = jmevm_classfile_get_utf8_copy(cf, utf8_idx);
        int32_t slen = (int32_t)strlen(s);
        int32_t barry_ref = jmevm_heap_alloc_array(JMEVM_OBJ_ARRAY_BYTE, slen);
        for (int32_t i = 0; i < slen; i++) {
          jmevm_array_store_byte(barry_ref, i, (int8_t)s[i]);
        }
        free(s);
        int32_t string_ref = jmevm_heap_alloc(NULL);
        jmevm_object_put_field(string_ref, 0, barry_ref);
        jmevm_object_put_field(string_ref, 1, slen);
        rc = frame_push(f, string_ref);
        if (rc != JMEVM_OK)
          return rc;
      }
      break;
    }

    case JMEVM_OP_NEWARRAY: {
      if (f->pc >= f->code_len)
        return JMEVM_ERR_TRUNCATED;
      uint8_t atype = f->code[f->pc++];
      int32_t length;
      rc = frame_pop(f, &length);
      if (rc != JMEVM_OK)
        return rc;
      jmevm_obj_type type =
          (atype == 10) ? JMEVM_OBJ_ARRAY_INT : JMEVM_OBJ_ARRAY_BYTE;
      int32_t array_ref = jmevm_heap_alloc_array(type, length);
      rc = frame_push(f, array_ref);
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_ARRAYLENGTH: {
      int32_t array_ref;
      rc = frame_pop(f, &array_ref);
      if (rc != JMEVM_OK)
        return rc;
      if (array_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      rc = frame_push(f, jmevm_array_length(array_ref));
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_IALOAD: {
      int32_t index, array_ref;
      rc = frame_pop(f, &index);
      if (rc != JMEVM_OK)
        return rc;
      if (array_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jmevm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jmevm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      rc = frame_push(f, jmevm_array_load_int(array_ref, index));
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_IASTORE: {
      int32_t value, index, array_ref;
      rc = frame_pop(f, &value);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &index);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JMEVM_OK)
        return rc;

      if (array_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jmevm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jmevm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      jmevm_array_store_int(array_ref, index, value);
      break;
    }

    case JMEVM_OP_BALOAD: {
      int32_t index, array_ref;
      rc = frame_pop(f, &index);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JMEVM_OK)
        return rc;

      if (array_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jmevm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jmevm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      rc = frame_push(f, (int32_t)jmevm_array_load_byte(array_ref, index));
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_BASTORE: {
      int32_t value, index, array_ref;
      rc = frame_pop(f, &value);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &index);
      if (rc != JMEVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JMEVM_OK)
        return rc;

      if (array_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jmevm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jmevm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      jmevm_array_store_byte(array_ref, index, (int8_t)value);
      break;
    }

    case JMEVM_OP_GETFIELD: {
      if (f->pc + 1 >= f->code_len) {
        return JMEVM_ERR_TRUNCATED;
      }
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      int32_t obj_ref;
      rc = frame_pop(f, &obj_ref);
      if (rc != JMEVM_OK)
        return rc;

      if (obj_ref == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      // Resolve field name and descriptor from Fieldref in current CP
      uint16_t nat_idx = cf->cp_fieldref_nat_index[fieldref_cp_index];
      uint16_t name_idx = cf->cp_nat_name_index[nat_idx];
      uint16_t desc_idx = cf->cp_nat_desc_index[nat_idx];

      char *name = jmevm_classfile_get_utf8_copy(cf, name_idx);
      char *desc = jmevm_classfile_get_utf8_copy(cf, desc_idx);

      // Resolve in target's class
      const struct jmevm_classfile *target_cf =
          jmevm_heap_get_classfile(obj_ref);
      int field_index = jmevm_classfile_resolve_field(target_cf, name, desc);
      free(name);
      free(desc);

      if (field_index < 0)
        return JMEVM_ERR_BAD_ARGS;

      int32_t val = jmevm_object_get_field(obj_ref, (uint16_t)field_index);
      rc = frame_push(f, val);
      if (rc != JMEVM_OK)
        return rc;
      break;
    }

    case JMEVM_OP_ATHROW: {
      int32_t ex_obj;
      rc = frame_pop(f, &ex_obj);
      if (rc != JMEVM_OK)
        return rc;

      if (ex_obj == 0) {
        jmevm_vm_throw_implicit(vm, "java/lang/NullPointerException");
      } else {
        vm->exception_obj = ex_obj;
      }
      continue;
    }

    default:
      return JMEVM_ERR_UNKNOWN_OPCODE;
    }
  }
}

jmevm_vm *jmevm_vm_create(void) {
  jmevm_vm *vm = (jmevm_vm *)calloc(1, sizeof(jmevm_vm));
  return vm;
}

void jmevm_vm_destroy(jmevm_vm *vm) {
  if (vm) {
    for (uint16_t i = 0; i < vm->class_count; i++) {
      // Note: we don't know if VM owns these or not.
      // Looking at execute_main, it used to destroy it.
      // We should decide who owns classes.
      // If main.c loads them and passes them, maybe main.c should destroy them.
      // But wait, the user said "meter todo eso al registro de clases".
    }
    free(vm);
  }
}

void jmevm_vm_register_class(jmevm_vm *vm, jmevm_classfile *cf) {
  if (vm && cf && vm->class_count < JMEVM_MAX_CLASSES) {
    vm->classes[vm->class_count++] = cf;
  }
}

static const struct jmevm_classfile *jmevm_vm_find_class(jmevm_vm *vm,
                                                         const char *name) {
  if (vm == NULL || name == NULL) {
    return NULL;
  }
  for (uint16_t i = 0; i < vm->class_count; i++) {
    const struct jmevm_classfile *cf = vm->classes[i];
    if (jmevm_classfile_utf8_equals(cf, cf->this_class_name_cp_index, name)) {
      return cf;
    }
  }
  return NULL;
}

int jmevm_vm_run_main(jmevm_vm *vm, const char *class_name) {
  const struct jmevm_classfile *cf = jmevm_vm_find_class(vm, class_name);
  if (cf == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }

  jmevm_main_method m;
  int rc = jmevm_classfile_extract_main(cf, &m);
  if (rc != 0) {
    return JMEVM_ERR_BAD_ARGS;
  }

  return jmevm_vm_run(vm, cf, m.code, m.code_len, m.max_locals, m.max_stack);
}

int jmevm_vm_run(jmevm_vm *vm, const jmevm_classfile *cf, const uint8_t *code,
                 size_t code_len, uint16_t max_locals, uint16_t max_stack) {
  if (vm == NULL || code == NULL || code_len == 0) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (cf == NULL) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (max_locals > JMEVM_MAX_LOCALS) {
    return JMEVM_ERR_BAD_ARGS;
  }
  if (max_stack == 0 || max_stack > JMEVM_MAX_STACK) {
    return JMEVM_ERR_BAD_ARGS;
  }

  jmevm_runtime_init_native();
  if (!jmevm_heap_is_initialized()) {
    jmevm_heap_init(64 * 1024);
  }

  vm->cf = cf;
  vm->frame_top = 1;

  jmevm_frame *f = &vm->frames[0];
  f->cf = cf;
  f->pc = 0;
  f->code = code;
  f->code_len = code_len;
  f->sp = 0;
  f->max_locals = max_locals;
  f->max_stack = max_stack;

  /* Clear locals for the new entry frame. */
  for (uint16_t li = 0; li < f->max_locals; li++) {
    f->locals[li] = 0;
  }

  return vm_exec(vm);
}

int jmevm_vm_get_local_i32(const jmevm_vm *vm, uint16_t index, int32_t *out) {
  if (vm == NULL || out == NULL) {
    return -1;
  }
  if (vm->frame_top == 0 && index >= vm->frames[0].max_locals) {
    return -1;
  }
  if (index >= vm->frames[0].max_locals) {
    return -1;
  }
  *out = vm->frames[0].locals[index];
  return 0;
}
