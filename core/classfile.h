#ifndef JMEVM_CORE_CLASSFILE_H
#define JMEVM_CORE_CLASSFILE_H

#include "jmevm.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal internal representation of a class file for this project:
 * enough to locate and extract the Code attribute for a single method:
 *   name: "main"
 *   descriptor: "([Ljava/lang/String;)V"
 */
struct jmevm_method {
    uint16_t name_cp_index;       /* CONSTANT_Utf8 */
    uint16_t descriptor_cp_index;/* CONSTANT_Utf8 */

    const uint8_t *code;
    size_t code_len;
    uint16_t max_stack;
    uint16_t max_locals;
};

struct jmevm_classfile {
    const uint8_t *buf;
    size_t len;

    uint16_t cp_count;
    uint8_t *cp_tag;         /* [cp_count] */
    uint32_t *cp_utf8_off;  /* [cp_count], valid only for CONSTANT_Utf8 */
    uint16_t *cp_utf8_len; /* [cp_count], valid only for CONSTANT_Utf8 */

    /* Minimal extra constant-pool data to resolve Methodref for invokestatic. */
    uint16_t *cp_class_name_index;       /* [cp_count] valid for CONSTANT_Class */
    uint16_t *cp_nat_name_index;        /* [cp_count] valid for CONSTANT_NameAndType */
    uint16_t *cp_nat_desc_index;        /* [cp_count] valid for CONSTANT_NameAndType */
    uint16_t *cp_methodref_class_index; /* [cp_count] valid for CONSTANT_Methodref */
    uint16_t *cp_methodref_nat_index;   /* [cp_count] valid for CONSTANT_Methodref */

    uint16_t this_class_name_cp_index; /* CONSTANT_Utf8 index for this class, or 0 */

    int has_main;
    const uint8_t *main_code;
    size_t main_code_len;
    uint16_t main_max_stack;
    uint16_t main_max_locals;

    uint16_t methods_count;
    struct jmevm_method *methods;
};

/* Lookup by constant-pool UTF8 indices (name+descriptor). */
const struct jmevm_method *jmevm_classfile_lookup_method(
    const struct jmevm_classfile *cf,
    uint16_t name_cp_index,
    uint16_t descriptor_cp_index);

#endif /* JMEVM_CORE_CLASSFILE_H */
