#include "core/bytecode.h"
#include "jmevm.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int read_file_bytes(const char *path, uint8_t **out_buf, size_t *out_len)
{
    if (path == NULL || out_buf == NULL || out_len == NULL) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }

    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    if (sz == 0) {
        fclose(f);
        return -1;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }

    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) {
        free(buf);
        return -1;
    }

    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1] == NULL) {
        fprintf(stderr, "Usage: %s <Main.class>\n", (argc > 0 && argv[0] != NULL) ? argv[0] : "jmevm");
        return 1;
    }

    const char *path = argv[1];

    uint8_t *class_buf = NULL;
    size_t class_len = 0;

    if (read_file_bytes(path, &class_buf, &class_len) != 0) {
        fprintf(stderr, "Failed to read class file: %s\n", path);
        return 1;
    }

    jmevm_vm *vm = jmevm_vm_create();
    if (vm == NULL) {
        free(class_buf);
        return 1;
    }

    int rc = jmevm_classfile_execute_main(vm, class_buf, class_len);

    for (uint16_t li = 0; li < 4; li++) {
        int32_t v = 0;
        if (jmevm_vm_get_local_i32(vm, li, &v) == 0) {
            printf("local[%u] = %d\n", (unsigned)li, (int)v);
        }
    }

    jmevm_vm_destroy(vm);
    free(class_buf);

    printf("run rc = %d (0 = success)\n", rc);
    return rc != 0 ? 1 : 0;
}
