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
struct jmevm_classfile {
    const uint8_t *buf;
    size_t len;

    uint16_t cp_count;
    uint8_t *cp_tag;         /* [cp_count] */
    uint32_t *cp_utf8_off;  /* [cp_count], valid only for CONSTANT_Utf8 */
    uint16_t *cp_utf8_len; /* [cp_count], valid only for CONSTANT_Utf8 */

    int has_main;
    const uint8_t *main_code;
    size_t main_code_len;
    uint16_t main_max_stack;
    uint16_t main_max_locals;
};

#endif /* JMEVM_CORE_CLASSFILE_H */
