#include "core/vm.h"
#include "core/classfile.h"
#include "core/bytecode.h"
#include "jmevm.h"

#include <stdlib.h>
#include <string.h>

#define JMEVM_MAX_CALLSTACK 16

struct jmevm_vm {
    const struct jmevm_classfile *cf;
    jmevm_frame frames[JMEVM_MAX_CALLSTACK];
    uint16_t frame_top; /* number of active frames */
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

static int frame_push(jmevm_frame *f, int32_t v)
{
    if (f->sp >= f->max_stack) {
        return JMEVM_ERR_STACK_OVERFLOW;
    }
    f->stack[f->sp++] = v;
    return JMEVM_OK;
}

static int frame_pop(jmevm_frame *f, int32_t *out)
{
    if (f->sp == 0) {
        return JMEVM_ERR_STACK_UNDERFLOW;
    }
    *out = f->stack[--f->sp];
    return JMEVM_OK;
}

/* Constant-pool tags used by invokestatic resolution. */
#define CONSTANT_Methodref 10
#define CONSTANT_InterfaceMethodref 11

static int parse_descriptor_minimal_i32_void(
    const uint8_t *desc,
    size_t desc_len,
    uint16_t *out_param_count,
    int *out_ret_is_int)
{
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
        if (desc[pos] != 'I') {
            /* Only int parameters are supported. */
            return JMEVM_ERR_BAD_ARGS;
        }
        param_count++;
        pos++;
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

static int resolve_invokestatic(
    const struct jmevm_classfile *cf,
    uint16_t methodref_cp_index,
    const struct jmevm_method **out_method,
    uint16_t *out_arg_count,
    int *out_ret_is_int)
{
    if (cf == NULL || out_method == NULL || out_arg_count == NULL || out_ret_is_int == NULL) {
        return JMEVM_ERR_BAD_ARGS;
    }
    if (methodref_cp_index == 0 || methodref_cp_index >= cf->cp_count) {
        return JMEVM_ERR_BAD_ARGS;
    }
    uint8_t tag = cf->cp_tag ? cf->cp_tag[methodref_cp_index] : 0;
    if (tag != CONSTANT_Methodref && tag != CONSTANT_InterfaceMethodref) {
        return JMEVM_ERR_BAD_ARGS;
    }

    uint16_t class_index = cf->cp_methodref_class_index ? cf->cp_methodref_class_index[methodref_cp_index] : 0;
    uint16_t nat_index = cf->cp_methodref_nat_index ? cf->cp_methodref_nat_index[methodref_cp_index] : 0;
    if (class_index == 0 || nat_index == 0) {
        return JMEVM_ERR_BAD_ARGS;
    }

    uint16_t method_name_cp_index = cf->cp_nat_name_index ? cf->cp_nat_name_index[nat_index] : 0;
    uint16_t desc_cp_index = cf->cp_nat_desc_index ? cf->cp_nat_desc_index[nat_index] : 0;
    if (method_name_cp_index == 0 || desc_cp_index == 0) {
        return JMEVM_ERR_BAD_ARGS;
    }

    uint16_t class_name_cp_index = cf->cp_class_name_index ? cf->cp_class_name_index[class_index] : 0;
    if (cf->this_class_name_cp_index != 0 && class_name_cp_index != cf->this_class_name_cp_index) {
        /* For now, only resolve within the same class. */
        return JMEVM_ERR_BAD_ARGS;
    }

    const struct jmevm_method *m = jmevm_classfile_lookup_method(cf, method_name_cp_index, desc_cp_index);
    if (m == NULL || m->code == NULL) {
        return JMEVM_ERR_BAD_ARGS;
    }

    if (desc_cp_index >= cf->cp_count || cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL) {
        return JMEVM_ERR_BAD_ARGS;
    }

    const uint8_t *desc_bytes = cf->buf + cf->cp_utf8_off[desc_cp_index];
    size_t desc_len = (size_t)cf->cp_utf8_len[desc_cp_index];

    uint16_t arg_count = 0;
    int ret_is_int = 0;
    int rc = parse_descriptor_minimal_i32_void(desc_bytes, desc_len, &arg_count, &ret_is_int);
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

static int vm_exec(struct jmevm_vm *vm)
{
    int rc;

    for (;;) {
        if (vm->frame_top == 0) {
            return JMEVM_OK;
        }

        jmevm_frame *f = &vm->frames[vm->frame_top - 1];
        if (f->pc >= f->code_len) {
            return JMEVM_ERR_TRUNCATED;
        }

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
        case JMEVM_OP_ILOAD_0:
            if (f->max_locals < 1) {
                return JMEVM_ERR_LOCAL_BOUNDS;
            }
            rc = frame_push(f, f->locals[0]);
            if (rc != JMEVM_OK) {
                return rc;
            }
            break;
        case JMEVM_OP_ILOAD_1:
            if (f->max_locals < 2) {
                return JMEVM_ERR_LOCAL_BOUNDS;
            }
            rc = frame_push(f, f->locals[1]);
            if (rc != JMEVM_OK) {
                return rc;
            }
            break;
        case JMEVM_OP_ILOAD_2:
            if (f->max_locals < 3) {
                return JMEVM_ERR_LOCAL_BOUNDS;
            }
            rc = frame_push(f, f->locals[2]);
            if (rc != JMEVM_OK) {
                return rc;
            }
            break;
        case JMEVM_OP_ILOAD_3:
            if (f->max_locals < 4) {
                return JMEVM_ERR_LOCAL_BOUNDS;
            }
            rc = frame_push(f, f->locals[3]);
            if (rc != JMEVM_OK) {
                return rc;
            }
            break;

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
            uint16_t methodref_cp_index = ((uint16_t)f->code[f->pc] << 8) | (uint16_t)f->code[f->pc + 1];
            f->pc += 2;

            const struct jmevm_method *m = NULL;
            uint16_t arg_count = 0;
            int ret_is_int = 0;
            rc = resolve_invokestatic(vm->cf, methodref_cp_index, &m, &arg_count, &ret_is_int);
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

        default:
            return JMEVM_ERR_UNKNOWN_OPCODE;
        }
    }
}

jmevm_vm *jmevm_vm_create(void)
{
    jmevm_vm *vm = (jmevm_vm *)calloc(1, sizeof(jmevm_vm));
    return vm;
}

void jmevm_vm_destroy(jmevm_vm *vm)
{
    free(vm);
}

int jmevm_vm_run(
    jmevm_vm *vm,
    const jmevm_classfile *cf,
    const uint8_t *code,
    size_t code_len,
    uint16_t max_locals,
    uint16_t max_stack)
{
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

    vm->cf = cf;
    vm->frame_top = 1;

    jmevm_frame *f = &vm->frames[0];
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

int jmevm_vm_get_local_i32(const jmevm_vm *vm, uint16_t index, int32_t *out)
{
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
