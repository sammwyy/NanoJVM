#include "core/vm.h"
#include "core/bytecode.h"
#include "core/classfile.h"
#include "core/gc.h"
#include "core/heap.h"
#include "core/runtime.h"
#include "loader/resource.h"
#include "nanojvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jvm_vm;
static const struct jvm_classfile *jvm_vm_find_class(struct jvm_vm *vm,
                                                     const char *name);

#define JVM_OK 0
#define JVM_ERR_UNKNOWN_OPCODE -1
#define JVM_ERR_STACK_OVERFLOW -2
#define JVM_ERR_STACK_UNDERFLOW -3
#define JVM_ERR_TRUNCATED -4
#define JVM_ERR_LOCAL_BOUNDS -5
#define JVM_ERR_BAD_ARGS -6
#define JVM_ERR_STACK_NOT_EMPTY -7
#define JVM_ERR_CALLSTACK_OVERFLOW -8

static int frame_push(jvm_frame *f, int32_t v) {
  if (f->sp >= f->max_stack) {
    return JVM_ERR_STACK_OVERFLOW;
  }
  f->stack[f->sp++] = v;
  return JVM_OK;
}

static int frame_pop(jvm_frame *f, int32_t *out) {
  if (f->sp == 0) {
    return JVM_ERR_STACK_UNDERFLOW;
  }
  *out = f->stack[--f->sp];
  return JVM_OK;
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
    return JVM_ERR_BAD_ARGS;
  }
  if (desc_len < 3) {
    return JVM_ERR_BAD_ARGS;
  }

  if (desc[0] != '(') {
    return JVM_ERR_BAD_ARGS;
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
        return JVM_ERR_BAD_ARGS;
      }
      pos++; /* consume ';' */
      param_count++;
    } else {
      /* Only int and String parameters are supported in this minimal VM. */
      return JVM_ERR_BAD_ARGS;
    }
    if (param_count > JVM_MAX_LOCALS) {
      return JVM_ERR_BAD_ARGS;
    }
  }
  if (pos >= desc_len || desc[pos] != ')') {
    return JVM_ERR_BAD_ARGS;
  }
  pos++; /* consume ')' */

  if (pos >= desc_len || (desc[pos] != 'I' && desc[pos] != 'V')) {
    return JVM_ERR_BAD_ARGS;
  }
  *out_ret_is_int = (desc[pos] == 'I');
  pos++;

  if (pos != desc_len) {
    return JVM_ERR_BAD_ARGS;
  }

  *out_param_count = param_count;
  return JVM_OK;
}

static int is_system_out_fieldref(const struct jvm_classfile *cf,
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
resolve_native_from_methodref(const struct jvm_classfile *cf,
                              uint16_t methodref_cp_index,
                              const struct jvm_native_method **out_native,
                              uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_native == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL) {
    return JVM_ERR_BAD_ARGS;
  }
  *out_native = NULL;
  *out_arg_count = 0;
  *out_ret_is_int = 0;

  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JVM_ERR_BAD_ARGS;
  }
  if (cf->cp_tag == NULL || cf->cp_methodref_class_index == NULL ||
      cf->cp_methodref_nat_index == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  uint8_t tag = cf->cp_tag[methodref_cp_index];
  if (tag != CONSTANT_Methodref && tag != CONSTANT_InterfaceMethodref) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t class_index = cf->cp_methodref_class_index
                             ? cf->cp_methodref_class_index[methodref_cp_index]
                             : 0;
  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (class_index == 0 || nat_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t class_name_cp_index =
      cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;
  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (class_name_cp_index == 0 || method_name_cp_index == 0 ||
      desc_cp_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  if (cf->cp_tag[class_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[method_name_cp_index] != CONSTANT_Utf8 ||
      cf->cp_tag[desc_cp_index] != CONSTANT_Utf8) {
    return JVM_ERR_BAD_ARGS;
  }
  if (cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL || cf->buf == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  const uint8_t *class_bytes = cf->buf + cf->cp_utf8_off[class_name_cp_index];
  size_t class_len = (size_t)cf->cp_utf8_len[class_name_cp_index];
  const uint8_t *method_bytes = cf->buf + cf->cp_utf8_off[method_name_cp_index];
  size_t method_len = (size_t)cf->cp_utf8_len[method_name_cp_index];
  const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_cp_index];
  size_t desc_len = (size_t)cf->cp_utf8_len[desc_cp_index];

  const struct jvm_native_method *nm = jvm_native_lookup_utf8(
      class_bytes, class_len, method_bytes, method_len, desc_bytes, desc_len);
  if (nm == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count,
                                             &ret_is_int);
  if (rc != JVM_OK) {
    return rc;
  }

  *out_native = nm;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JVM_OK;
}

static int resolve_invokestatic(struct jvm_vm *vm,
                                const struct jvm_classfile *cf,
                                uint16_t methodref_cp_index,
                                const struct jvm_classfile **out_classfile,
                                const struct jvm_method **out_method,
                                uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_method == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL) {
    return JVM_ERR_BAD_ARGS;
  }
  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JVM_ERR_BAD_ARGS;
  }
  uint8_t tag = cf->cp_tag ? cf->cp_tag[methodref_cp_index] : 0;
  if (tag != CONSTANT_Methodref && tag != CONSTANT_InterfaceMethodref) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t class_index = cf->cp_methodref_class_index
                             ? cf->cp_methodref_class_index[methodref_cp_index]
                             : 0;
  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (class_index == 0 || nat_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (method_name_cp_index == 0 || desc_cp_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  /* Determine which class owns the static method.
   * If it is the same class as the caller, search directly.
   * Otherwise use the VM registry for cross-class resolution. */
  uint16_t class_name_cp_index =
      cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;

  const struct jvm_classfile *target_cf = cf; /* default: same class */
  if (class_name_cp_index != 0 &&
      class_name_cp_index != cf->this_class_name_cp_index) {
    /* Cross-class static call: look up the target class in the VM registry. */
    char *target_class_name =
        jvm_classfile_get_utf8_copy(cf, class_name_cp_index);
    if (target_class_name) {
      const struct jvm_classfile *found =
          jvm_vm_find_class(vm, target_class_name);
      free(target_class_name);
      if (found)
        target_cf = found;
    }
  }

  char *method_name = jvm_classfile_get_utf8_copy(cf, method_name_cp_index);
  char *method_desc = jvm_classfile_get_utf8_copy(cf, desc_cp_index);
  const struct jvm_method *m =
      jvm_classfile_resolve_method(target_cf, method_name, method_desc);
  free(method_name);
  free(method_desc);

  if (m == NULL || m->code == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  if (desc_cp_index >= cf->cp_count || cf->cp_utf8_off == NULL ||
      cf->cp_utf8_len == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_cp_index];
  size_t desc_len = (size_t)cf->cp_utf8_len[desc_cp_index];

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count,
                                             &ret_is_int);
  if (rc != JVM_OK) {
    return rc;
  }

  if (arg_count > m->max_locals) {
    return JVM_ERR_BAD_ARGS;
  }
  if (m->max_locals > JVM_MAX_LOCALS || m->max_stack > JVM_MAX_STACK) {
    return JVM_ERR_BAD_ARGS;
  }

  if (out_classfile)
    *out_classfile = target_cf;
  *out_method = m;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JVM_OK;
}

static int resolve_invokevirtual(const struct jvm_classfile *cf,
                                 uint16_t methodref_cp_index,
                                 const struct jvm_classfile *obj_cf,
                                 const struct jvm_method **out_method,
                                 uint16_t *out_arg_count, int *out_ret_is_int) {
  if (cf == NULL || out_method == NULL || out_arg_count == NULL ||
      out_ret_is_int == NULL || obj_cf == NULL) {
    return JVM_ERR_BAD_ARGS;
  }
  if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t nat_index = cf->cp_methodref_nat_index
                           ? cf->cp_methodref_nat_index[methodref_cp_index]
                           : 0;
  if (nat_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t method_name_cp_index =
      cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
  uint16_t desc_cp_index =
      cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
  if (method_name_cp_index == 0 || desc_cp_index == 0) {
    return JVM_ERR_BAD_ARGS;
  }

  char *method_name = jvm_classfile_get_utf8_copy(cf, method_name_cp_index);
  char *method_desc = jvm_classfile_get_utf8_copy(cf, desc_cp_index);

  const struct jvm_method *m =
      jvm_classfile_resolve_method(obj_cf, method_name, method_desc);

  if (m == NULL || m->code == NULL) {
    free(method_name);
    free(method_desc);
    return JVM_ERR_BAD_ARGS;
  }

  uint16_t arg_count = 0;
  int ret_is_int = 0;
  int rc = parse_descriptor_minimal_i32_void((const uint8_t *)method_desc,
                                             strlen(method_desc), &arg_count,
                                             &ret_is_int);

  free(method_name);
  free(method_desc);

  if (rc != JVM_OK) {
    return rc;
  }

  if (m->max_locals > JVM_MAX_LOCALS || m->max_stack > JVM_MAX_STACK) {
    return JVM_ERR_BAD_ARGS;
  }

  *out_method = m;
  *out_arg_count = arg_count;
  *out_ret_is_int = ret_is_int;
  return JVM_OK;
}

static int class_is_assignable_to(struct jvm_vm *vm,
                                  const struct jvm_classfile *child,
                                  const char *parent_name) {
  while (child != NULL) {
    if (jvm_classfile_utf8_equals(child, child->this_class_name_cp_index,
                                  parent_name)) {
      return 1;
    }
    if (child->super_class_name_cp_index == 0)
      break;
    char *super_name =
        jvm_classfile_get_utf8_copy(child, child->super_class_name_cp_index);
    const struct jvm_classfile *super_cf = jvm_vm_find_class(vm, super_name);
    free(super_name);
    child = super_cf;
  }
  return 0;
}

static int vm_handle_exception(struct jvm_vm *vm) {
  if (vm->exception_obj == 0) {
    return JVM_OK;
  }

  int32_t ex_obj = vm->exception_obj;
  const struct jvm_classfile *ex_cf = jvm_heap_get_classfile(ex_obj);
  if (ex_cf == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  while (vm->frame_top > 0) {
    jvm_frame *f = &vm->frames[vm->frame_top - 1];
    const struct jvm_classfile *cf = f->cf;

    /* Find the method that this frame belongs to.
     * In this minimal VM, frames don't store the method pointer directly,
     * but we can find it by matching the code pointer. */
    const struct jvm_method *m = NULL;
    for (uint16_t i = 0; i < cf->methods_count; i++) {
      if (cf->methods[i].code == f->code) {
        m = &cf->methods[i];
        break;
      }
    }

    if (m != NULL) {
      for (uint16_t i = 0; i < m->exception_table_length; i++) {
        struct jvm_exception_handler *h = &m->exception_table[i];
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
            char *catch_name = jvm_classfile_get_utf8_copy(cf, catch_name_idx);
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
            return JVM_OK;
          }
        }
      }
    }

    /* Not found in this frame, unwind. */
    vm->frame_top--;
  }

  fprintf(stderr, "Uncaught exception: 0x%x\n", (unsigned int)ex_obj);
  return -1; /* Terminate VM */
}

static void jvm_vm_throw_implicit(struct jvm_vm *vm, const char *class_name) {
  const struct jvm_classfile *ex_cf = jvm_vm_find_class(vm, class_name);
  if (ex_cf == NULL) {
    /* Exception class not loaded — allocate a sentinel object with no class.
     * In a full VM we'd load the exception class on demand. Here we use a
     * non-zero object reference so exception handling still fires correctly. */
    fprintf(stderr,
            "[implicit exception] %s (class not loaded, using sentinel)\n",
            class_name);
    vm->exception_obj = (int32_t)0xdeadbeef; /* sentinel non-zero value */
    return;
  }
  vm->exception_obj = jvm_heap_alloc(ex_cf);
}

static int vm_exec(struct jvm_vm *vm) {
  int rc;

  for (;;) {
    if (vm->frame_top == 0) {
      return JVM_OK;
    }

    if (vm->exception_obj != 0) {
      rc = vm_handle_exception(vm);
      if (rc != JVM_OK) {
        return rc;
      }
      continue;
    }

    jvm_frame *f = &vm->frames[vm->frame_top - 1];
    if (f->pc >= f->code_len) {
      return JVM_ERR_TRUNCATED;
    }

    const struct jvm_classfile *cf = f->cf;
    uint8_t op = f->code[f->pc++];

    switch (op) {
    case JVM_OP_ICONST_M1:
      rc = frame_push(f, -1);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_0:
      rc = frame_push(f, 0);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_1:
      rc = frame_push(f, 1);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_2:
      rc = frame_push(f, 2);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_3:
      rc = frame_push(f, 3);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_4:
      rc = frame_push(f, 4);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ICONST_5:
      rc = frame_push(f, 5);
      if (rc != JVM_OK) {
        return rc;
      }
      break;

    case JVM_OP_BIPUSH: {
      if (f->pc >= f->code_len)
        return JVM_ERR_TRUNCATED;
      int8_t v = (int8_t)f->code[f->pc++];
      rc = frame_push(f, (int32_t)v);
      if (rc != JVM_OK)
        return rc;
      break;
    }
    case JVM_OP_SIPUSH: {
      if (f->pc + 1 >= f->code_len)
        return JVM_ERR_TRUNCATED;
      int16_t v = ((int16_t)f->code[f->pc] << 8) | (int16_t)f->code[f->pc + 1];
      f->pc += 2;
      rc = frame_push(f, (int32_t)v);
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_ALOAD:
    case JVM_OP_ILOAD: {
      if (f->pc >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint8_t idx = f->code[f->pc++];
      if (idx >= f->max_locals) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[idx]);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    }
    case JVM_OP_ALOAD_0:
    case JVM_OP_ILOAD_0:
      if (f->max_locals < 1) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[0]);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ALOAD_1:
    case JVM_OP_ILOAD_1:
      if (f->max_locals < 2) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[1]);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ALOAD_2:
    case JVM_OP_ILOAD_2:
      if (f->max_locals < 3) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[2]);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    case JVM_OP_ALOAD_3:
    case JVM_OP_ILOAD_3:
      if (f->max_locals < 4) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      rc = frame_push(f, f->locals[3]);
      if (rc != JVM_OK) {
        return rc;
      }
      break;

    case JVM_OP_DUP: {
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK)
        return rc;
      rc = frame_push(f, v);
      if (rc != JVM_OK)
        return rc;
      rc = frame_push(f, v);
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_ASTORE:
    case JVM_OP_ISTORE: {
      if (f->pc >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint8_t idx = f->code[f->pc++];
      if (idx >= f->max_locals) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK) {
        return rc;
      }
      f->locals[idx] = v;
      break;
    }
    case JVM_OP_ASTORE_0:
    case JVM_OP_ISTORE_0: {
      if (f->max_locals < 1) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK) {
        return rc;
      }
      f->locals[0] = v;
      break;
    }
    case JVM_OP_ASTORE_1:
    case JVM_OP_ISTORE_1: {
      if (f->max_locals < 2) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK) {
        return rc;
      }
      f->locals[1] = v;
      break;
    }
    case JVM_OP_ASTORE_2:
    case JVM_OP_ISTORE_2: {
      if (f->max_locals < 3) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK) {
        return rc;
      }
      f->locals[2] = v;
      break;
    }
    case JVM_OP_ASTORE_3:
    case JVM_OP_ISTORE_3: {
      if (f->max_locals < 4) {
        return JVM_ERR_LOCAL_BOUNDS;
      }
      int32_t v;
      rc = frame_pop(f, &v);
      if (rc != JVM_OK) {
        return rc;
      }
      f->locals[3] = v;
      break;
    }

    case JVM_OP_IADD: {
      int32_t a, b;
      rc = frame_pop(f, &b);
      if (rc != JVM_OK) {
        return rc;
      }
      rc = frame_pop(f, &a);
      if (rc != JVM_OK) {
        return rc;
      }
      rc = frame_push(f, a + b);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    }
    case JVM_OP_ISUB: {
      int32_t a, b;
      rc = frame_pop(f, &b);
      if (rc != JVM_OK) {
        return rc;
      }
      rc = frame_pop(f, &a);
      if (rc != JVM_OK) {
        return rc;
      }
      rc = frame_push(f, a - b);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    }
    case JVM_OP_INVOKESTATIC: {
      if (f->pc + 1 >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint16_t methodref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jvm_native_method *nm = NULL;
      uint16_t arg_count = 0;
      int ret_is_int = 0;

      rc = resolve_native_from_methodref(cf, methodref_cp_index, &nm,
                                         &arg_count, &ret_is_int);
      if (rc == JVM_OK && nm != NULL) {
        /* Native static method: no receiver on the operand stack. */
        if (arg_count > f->sp) {
          return JVM_ERR_STACK_UNDERFLOW;
        }
        int32_t args[JVM_MAX_LOCALS];
        for (int i = (int)arg_count - 1; i >= 0; i--) {
          rc = frame_pop(f, &args[(size_t)i]);
          if (rc != JVM_OK) {
            return rc;
          }
        }
        int32_t ret = nm->fn(vm, 0, args, arg_count);
        if (ret_is_int) {
          rc = frame_push(f, ret);
          if (rc != JVM_OK) {
            return rc;
          }
        }
        continue;
      }

      const struct jvm_classfile *static_target_cf = NULL;
      const struct jvm_method *m = NULL;
      rc = resolve_invokestatic(vm, cf, methodref_cp_index, &static_target_cf,
                                &m, &arg_count, &ret_is_int);
      if (rc != JVM_OK) {
        return rc;
      }
      (void)ret_is_int;

      if (arg_count > f->sp) {
        return JVM_ERR_STACK_UNDERFLOW;
      }
      if (vm->frame_top >= JVM_MAX_CALLSTACK) {
        return JVM_ERR_CALLSTACK_OVERFLOW;
      }

      int32_t args[JVM_MAX_LOCALS];
      for (int i = (int)arg_count - 1; i >= 0; i--) {
        rc = frame_pop(f, &args[(size_t)i]);
        if (rc != JVM_OK) {
          return rc;
        }
      }

      jvm_frame *callee = &vm->frames[vm->frame_top++];
      if (m->max_locals > JVM_MAX_LOCALS || m->max_stack > JVM_MAX_STACK) {
        return JVM_ERR_BAD_ARGS;
      }

      callee->cf = static_target_cf ? static_target_cf : cf;
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

    case JVM_OP_GETSTATIC: {
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      /* Minimal support: only java/lang/System.out. */
      if (is_system_out_fieldref(cf, fieldref_cp_index)) {
        rc = frame_push(f, jvm_runtime_stub_system_out());
        if (rc != JVM_OK) {
          return rc;
        }
        continue;
      }
      return JVM_ERR_BAD_ARGS;
    }

    case JVM_OP_INVOKESPECIAL:
    case JVM_OP_INVOKEVIRTUAL: {
      if (f->pc + 1 >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint16_t methodref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jvm_native_method *nm = NULL;
      uint16_t arg_count = 0;
      int ret_is_int = 0;

      // Try native first
      rc = resolve_native_from_methodref(cf, methodref_cp_index, &nm,
                                         &arg_count, &ret_is_int);
      if (rc == JVM_OK && nm != NULL) {
        /* Virtual methods: stack has receiver + descriptor params. */
        if ((uint16_t)(arg_count + 1) > f->sp) {
          return JVM_ERR_STACK_UNDERFLOW;
        }

        int32_t args[JVM_MAX_LOCALS];
        for (int i = (int)arg_count - 1; i >= 0; i--) {
          rc = frame_pop(f, &args[(size_t)i]);
          if (rc != JVM_OK) {
            return rc;
          }
        }
        int32_t receiver = 0;
        rc = frame_pop(f, &receiver);
        if (rc != JVM_OK) {
          return rc;
        }

        if (receiver == 0) {
          jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
          continue;
        }

        int32_t ret = nm->fn(vm, receiver, args, arg_count);
        if (ret_is_int) {
          rc = frame_push(f, ret);
          if (rc != JVM_OK) {
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
      if (rc != JVM_OK) {
        return rc;
      }

      if (f->sp < (uint16_t)(arg_count + 1)) {
        return JVM_ERR_STACK_UNDERFLOW;
      }

      int32_t receiver = f->stack[f->sp - arg_count - 1];
      if (receiver == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      const struct jvm_classfile *target_class = NULL;
      uint8_t op_prev = f->code[f->pc - 3];

      if (op_prev == JVM_OP_INVOKESPECIAL) {
        uint16_t class_cp_idx =
            cf->cp_methodref_class_index[methodref_cp_index];
        uint16_t class_name_idx = cf->cp_class_name_index[class_cp_idx];
        char *target_class_name =
            jvm_classfile_get_utf8_copy(cf, class_name_idx);
        target_class = jvm_vm_find_class(vm, target_class_name);
        free(target_class_name);
      } else {
        target_class = jvm_heap_get_classfile(receiver);
      }

      if (target_class == NULL && op_prev != JVM_OP_INVOKESPECIAL) {
        // Fallback for INVOKEVIRTUAL stubs (like PrintStream)
        target_class = cf;
      }

      const struct jvm_method *m = NULL;
      rc = resolve_invokevirtual(cf, methodref_cp_index, target_class, &m,
                                 &arg_count, &ret_is_int);
      if (rc != JVM_OK) {
        // Special case: if it's <init> and not found, maybe it's
        // java/lang/Object.<init> which we can safely ignore in this minimal
        // VM.
        char *mname =
            jvm_classfile_get_utf8_copy(cf, cf->cp_nat_name_index[nat_index]);
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

      if (vm->frame_top >= JVM_MAX_CALLSTACK) {
        return JVM_ERR_CALLSTACK_OVERFLOW;
      }

      int32_t args[JVM_MAX_LOCALS];
      for (int i = (int)arg_count - 1; i >= 0; i--) {
        rc = frame_pop(f, &args[(size_t)i]);
        if (rc != JVM_OK) {
          return rc;
        }
      }
      int32_t discard_receiver;
      rc = frame_pop(f, &discard_receiver);
      if (rc != JVM_OK) {
        return rc;
      }

      jvm_frame *callee = &vm->frames[vm->frame_top++];
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

    case JVM_OP_IRETURN: {
      if (f->sp != 1) {
        return JVM_ERR_STACK_NOT_EMPTY;
      }
      int32_t ret = 0;
      rc = frame_pop(f, &ret);
      if (rc != JVM_OK) {
        return rc;
      }

      vm->frame_top--;
      if (vm->frame_top == 0) {
        /* Top-level ireturn isn't currently exposed; treat as success. */
        return JVM_OK;
      }

      jvm_frame *caller = &vm->frames[vm->frame_top - 1];
      rc = frame_push(caller, ret);
      if (rc != JVM_OK) {
        return rc;
      }
      continue;
    }

    case JVM_OP_RETURN:
      if (f->sp != 0) {
        return JVM_ERR_STACK_NOT_EMPTY;
      }
      vm->frame_top--;
      if (vm->frame_top == 0) {
        return JVM_OK;
      }
      continue;

    case JVM_OP_NEW: {
      if (f->pc + 1 >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      // uint16_t class_index = ((uint16_t)f->code[f->pc] << 8) |
      // (uint16_t)f->code[f->pc + 1];
      uint16_t class_cp_idx =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      const struct jvm_classfile *target_cf = NULL;
      uint16_t class_name_idx = cf->cp_class_name_index[class_cp_idx];
      char *class_name = jvm_classfile_get_utf8_copy(cf, class_name_idx);

      target_cf = jvm_vm_find_class(vm, class_name);
      free(class_name);

      if (target_cf == NULL) {
        return JVM_ERR_BAD_ARGS;
      }

      int32_t obj_ref = jvm_heap_alloc(target_cf);
      if (obj_ref == 0) {
        jvm_gc_run(vm);
        obj_ref = jvm_heap_alloc(target_cf);
        if (obj_ref == 0) {
          return JVM_ERR_UNKNOWN_OPCODE; // Treat as OOM or general error
        }
      }
      rc = frame_push(f, obj_ref);
      if (rc != JVM_OK) {
        return rc;
      }
      break;
    }

    case JVM_OP_PUTFIELD: {
      if (f->pc + 1 >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      int32_t val, obj_ref;
      rc = frame_pop(f, &val);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &obj_ref);
      if (rc != JVM_OK)
        return rc;

      if (obj_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      // Resolve field name and descriptor from Fieldref in current CP
      uint16_t nat_idx = cf->cp_fieldref_nat_index[fieldref_cp_index];
      uint16_t name_idx = cf->cp_nat_name_index[nat_idx];
      uint16_t desc_idx = cf->cp_nat_desc_index[nat_idx];

      char *name = jvm_classfile_get_utf8_copy(cf, name_idx);
      char *desc = jvm_classfile_get_utf8_copy(cf, desc_idx);

      // Resolve in target's class
      const struct jvm_classfile *target_cf = jvm_heap_get_classfile(obj_ref);
      int field_index = jvm_classfile_resolve_field(target_cf, name, desc);
      free(name);
      free(desc);

      if (field_index < 0)
        return JVM_ERR_BAD_ARGS;

      jvm_object_put_field(obj_ref, (uint16_t)field_index, val);
      break;
    }

    case JVM_OP_LDC: {
      if (f->pc >= f->code_len)
        return JVM_ERR_TRUNCATED;
      uint8_t cp_idx = f->code[f->pc++];
      uint8_t tag = cf->cp_tag[cp_idx];
      if (tag == CONSTANT_Integer) {
        rc = frame_push(f, cf->cp_integer[cp_idx]);
        if (rc != JVM_OK)
          return rc;
      } else if (tag == CONSTANT_String) {
        uint16_t utf8_idx = cf->cp_string_index[cp_idx];
        char *s = jvm_classfile_get_utf8_copy(cf, utf8_idx);
        int32_t slen = (int32_t)strlen(s);
        int32_t barry_ref = jvm_heap_alloc_array(JVM_OBJ_ARRAY_BYTE, slen);
        if (barry_ref == 0) {
          jvm_gc_run(vm);
          barry_ref = jvm_heap_alloc_array(JVM_OBJ_ARRAY_BYTE, slen);
          if (barry_ref == 0)
            return JVM_ERR_UNKNOWN_OPCODE;
        }
        for (int32_t i = 0; i < slen; i++) {
          jvm_array_store_byte(barry_ref, i, (int8_t)s[i]);
        }
        free(s);
        int32_t string_ref = jvm_heap_alloc(NULL);
        if (string_ref == 0) {
          jvm_gc_run(vm);
          string_ref = jvm_heap_alloc(NULL);
          if (string_ref == 0)
            return JVM_ERR_UNKNOWN_OPCODE;
        }
        jvm_object_put_field(string_ref, 0, barry_ref);
        jvm_object_put_field(string_ref, 1, slen);
        rc = frame_push(f, string_ref);
        if (rc != JVM_OK)
          return rc;
      }
      break;
    }

    case JVM_OP_NEWARRAY: {
      if (f->pc >= f->code_len)
        return JVM_ERR_TRUNCATED;
      uint8_t atype = f->code[f->pc++];
      int32_t length;
      rc = frame_pop(f, &length);
      if (rc != JVM_OK)
        return rc;
      jvm_obj_type type =
          (atype == 10) ? JVM_OBJ_ARRAY_INT : JVM_OBJ_ARRAY_BYTE;
      int32_t array_ref = jvm_heap_alloc_array(type, length);
      if (array_ref == 0) {
        jvm_gc_run(vm);
        array_ref = jvm_heap_alloc_array(type, length);
        if (array_ref == 0) {
          return JVM_ERR_UNKNOWN_OPCODE;
        }
      }
      rc = frame_push(f, array_ref);
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_ARRAYLENGTH: {
      int32_t array_ref;
      rc = frame_pop(f, &array_ref);
      if (rc != JVM_OK)
        return rc;
      if (array_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      rc = frame_push(f, jvm_array_length(array_ref));
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_IALOAD: {
      int32_t index, array_ref;
      rc = frame_pop(f, &index);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JVM_OK)
        return rc;
      if (array_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jvm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jvm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      rc = frame_push(f, jvm_array_load_int(array_ref, index));
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_IASTORE: {
      int32_t value, index, array_ref;
      rc = frame_pop(f, &value);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &index);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JVM_OK)
        return rc;

      if (array_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jvm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jvm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      jvm_array_store_int(array_ref, index, value);
      break;
    }

    case JVM_OP_BALOAD: {
      int32_t index, array_ref;
      rc = frame_pop(f, &index);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JVM_OK)
        return rc;

      if (array_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jvm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jvm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      rc = frame_push(f, (int32_t)jvm_array_load_byte(array_ref, index));
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_BASTORE: {
      int32_t value, index, array_ref;
      rc = frame_pop(f, &value);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &index);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &array_ref);
      if (rc != JVM_OK)
        return rc;

      if (array_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }
      int32_t len = jvm_array_length(array_ref);
      if (index < 0 || index >= len) {
        jvm_vm_throw_implicit(vm, "java/lang/ArrayIndexOutOfBoundsException");
        continue;
      }
      jvm_array_store_byte(array_ref, index, (int8_t)value);
      break;
    }

    case JVM_OP_GETFIELD: {
      if (f->pc + 1 >= f->code_len) {
        return JVM_ERR_TRUNCATED;
      }
      uint16_t fieldref_cp_index =
          ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
      f->pc += 2;

      int32_t obj_ref;
      rc = frame_pop(f, &obj_ref);
      if (rc != JVM_OK)
        return rc;

      if (obj_ref == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
        continue;
      }

      // Resolve field name and descriptor from Fieldref in current CP
      uint16_t nat_idx = cf->cp_fieldref_nat_index[fieldref_cp_index];
      uint16_t name_idx = cf->cp_nat_name_index[nat_idx];
      uint16_t desc_idx = cf->cp_nat_desc_index[nat_idx];

      char *name = jvm_classfile_get_utf8_copy(cf, name_idx);
      char *desc = jvm_classfile_get_utf8_copy(cf, desc_idx);

      // Resolve in target's class
      const struct jvm_classfile *target_cf = jvm_heap_get_classfile(obj_ref);
      int field_index = jvm_classfile_resolve_field(target_cf, name, desc);
      free(name);
      free(desc);

      if (field_index < 0)
        return JVM_ERR_BAD_ARGS;

      int32_t val = jvm_object_get_field(obj_ref, (uint16_t)field_index);
      rc = frame_push(f, val);
      if (rc != JVM_OK)
        return rc;
      break;
    }

    case JVM_OP_ATHROW: {
      int32_t ex_obj;
      rc = frame_pop(f, &ex_obj);
      if (rc != JVM_OK)
        return rc;

      if (ex_obj == 0) {
        jvm_vm_throw_implicit(vm, "java/lang/NullPointerException");
      } else {
        vm->exception_obj = ex_obj;
      }
      continue;
    }

    case JVM_OP_IINC: {
      if (f->pc + 1 >= f->code_len)
        return JVM_ERR_TRUNCATED;
      uint8_t idx = f->code[f->pc++];
      int8_t val = (int8_t)f->code[f->pc++];
      if (idx >= f->max_locals)
        return JVM_ERR_LOCAL_BOUNDS;
      f->locals[idx] += val;
      break;
    }

    case JVM_OP_IF_ICMPGE: {
      if (f->pc + 1 >= f->code_len)
        return JVM_ERR_TRUNCATED;
      int16_t offset = (int16_t)(((uint16_t)f->code[f->pc] << 8) |
                                 (uint16_t)f->code[f->pc + 1]);
      f->pc += 2;
      int32_t v1, v2;
      rc = frame_pop(f, &v2);
      if (rc != JVM_OK)
        return rc;
      rc = frame_pop(f, &v1);
      if (rc != JVM_OK)
        return rc;
      if (v1 >= v2) {
        f->pc = (uint32_t)((int32_t)(f->pc - 3) + offset);
      }
      break;
    }

    case JVM_OP_GOTO: {
      if (f->pc + 1 >= f->code_len)
        return JVM_ERR_TRUNCATED;
      int16_t offset = (int16_t)(((uint16_t)f->code[f->pc] << 8) |
                                 (uint16_t)f->code[f->pc + 1]);
      f->pc += 2;
      f->pc = (uint32_t)((int32_t)(f->pc - 3) + offset);
      break;
    }

    default:
      fprintf(stderr, "Unknown opcode: %02x\n", op);
      return JVM_ERR_UNKNOWN_OPCODE;
    }
  }
}

jvm_vm *jvm_vm_create(void) {
  jvm_vm *vm = (jvm_vm *)calloc(1, sizeof(jvm_vm));
  extern void jvm_cldc_init(jvm_vm * vm);
  jvm_cldc_init(vm);
  return vm;
}

void jvm_vm_destroy(jvm_vm *vm) {
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

void jvm_vm_register_class(jvm_vm *vm, jvm_classfile *cf) {
  if (vm && cf && vm->class_count < JVM_MAX_CLASSES) {
    vm->classes[vm->class_count++] = cf;
  }
}

static const struct jvm_classfile *jvm_vm_find_class(jvm_vm *vm,
                                                     const char *name) {
  if (vm == NULL || name == NULL) {
    return NULL;
  }
  for (uint16_t i = 0; i < vm->class_count; i++) {
    const struct jvm_classfile *cf = vm->classes[i];
    if (jvm_classfile_utf8_equals(cf, cf->this_class_name_cp_index, name)) {
      return cf;
    }
  }

  /* Not found — try lazy loading from the attached classpath. */
  if (vm->classpath) {
    size_t class_len = 0;
    uint8_t *class_buf =
        jvm_classpath_find_class(vm->classpath, name, &class_len);
    if (class_buf) {
      jvm_classfile *cf = jvm_classfile_load_from_buffer(class_buf, class_len);
      if (cf) {
        jvm_vm_register_class(vm, cf);
        return cf;
      }
      free(class_buf);
    }
  }

  return NULL;
}

void jvm_vm_set_classpath(jvm_vm *vm, struct jvm_classpath *cp) {
  if (vm)
    vm->classpath = cp;
}

int jvm_vm_run_main(jvm_vm *vm, const char *class_name) {
  const struct jvm_classfile *cf = jvm_vm_find_class(vm, class_name);
  if (cf == NULL) {
    return JVM_ERR_BAD_ARGS;
  }

  jvm_main_method m;
  int rc = jvm_classfile_extract_main(cf, &m);
  if (rc != 0) {
    return JVM_ERR_BAD_ARGS;
  }

  return jvm_vm_run(vm, cf, m.code, m.code_len, m.max_locals, m.max_stack);
}

int jvm_vm_run(jvm_vm *vm, const jvm_classfile *cf, const uint8_t *code,
               size_t code_len, uint16_t max_locals, uint16_t max_stack) {
  if (vm == NULL || code == NULL || code_len == 0) {
    return JVM_ERR_BAD_ARGS;
  }
  if (cf == NULL) {
    return JVM_ERR_BAD_ARGS;
  }
  if (max_locals > JVM_MAX_LOCALS) {
    return JVM_ERR_BAD_ARGS;
  }
  if (max_stack == 0 || max_stack > JVM_MAX_STACK) {
    return JVM_ERR_BAD_ARGS;
  }

  jvm_runtime_init_native();
  if (!jvm_heap_is_initialized()) {
    jvm_heap_init(64 * 1024);
  }

  vm->cf = cf;
  vm->frame_top = 1;

  jvm_frame *f = &vm->frames[0];
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

int jvm_vm_get_local_i32(const jvm_vm *vm, uint16_t index, int32_t *out) {
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
