#include "core/vm.h"
#include "core/bytecode.h"
#include "jmevm.h"

#include <stdlib.h>
#include <string.h>

struct jmevm_vm {
    jmevm_frame frame;
};

#define JMEVM_OK 0
#define JMEVM_ERR_UNKNOWN_OPCODE -1
#define JMEVM_ERR_STACK_OVERFLOW -2
#define JMEVM_ERR_STACK_UNDERFLOW -3
#define JMEVM_ERR_TRUNCATED -4
#define JMEVM_ERR_LOCAL_BOUNDS -5
#define JMEVM_ERR_BAD_ARGS -6
#define JMEVM_ERR_STACK_NOT_EMPTY -7

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

static int frame_exec(jmevm_frame *f)
{
    int rc;

    for (;;) {
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

        case JMEVM_OP_RETURN:
            if (f->sp != 0) {
                return JMEVM_ERR_STACK_NOT_EMPTY;
            }
            return JMEVM_OK;

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
    const uint8_t *code,
    size_t code_len,
    uint16_t max_locals,
    uint16_t max_stack
)
{
    if (vm == NULL || code == NULL || code_len == 0) {
        return JMEVM_ERR_BAD_ARGS;
    }
    if (max_locals > JMEVM_MAX_LOCALS) {
        return JMEVM_ERR_BAD_ARGS;
    }
    if (max_stack == 0 || max_stack > JMEVM_MAX_STACK) {
        return JMEVM_ERR_BAD_ARGS;
    }

    jmevm_frame *f = &vm->frame;
    memset(f->locals, 0, sizeof(f->locals));
    f->pc = 0;
    f->code = code;
    f->code_len = code_len;
    f->sp = 0;
    f->max_locals = max_locals;
    f->max_stack = max_stack;

    return frame_exec(f);
}

int jmevm_vm_get_local_i32(const jmevm_vm *vm, uint16_t index, int32_t *out)
{
    if (vm == NULL || out == NULL) {
        return -1;
    }
    if (index >= vm->frame.max_locals) {
        return -1;
    }
    *out = vm->frame.locals[index];
    return 0;
}
