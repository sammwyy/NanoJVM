#include "core/bytecode.h"
#include "jmevm.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t code[] = {
        JMEVM_OP_ICONST_2,
        JMEVM_OP_ICONST_3,
        JMEVM_OP_IADD,
        JMEVM_OP_ISTORE_0,
        JMEVM_OP_RETURN,
    };

    jmevm_vm *vm = jmevm_vm_create();
    if (vm == NULL) {
        return 1;
    }

    int rc = jmevm_vm_run(vm, code, sizeof(code), 1, 2);
    int32_t a = 0;
    jmevm_vm_get_local_i32(vm, 0, &a);
    jmevm_vm_destroy(vm);

    printf("local[0] = %d (expected 5)\n", (int)a);
    printf("run rc = %d (0 = success)\n", rc);

    return (rc != 0 || a != 5) ? 1 : 0;
}
