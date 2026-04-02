#include "core/classfile.h"

#include <stdlib.h>
#include <string.h>

#define JMEVM_CLASSFILE_OK 0

#define JMEVM_CLASSFILE_ERR_BAD_ARGS -1
#define JMEVM_CLASSFILE_ERR_TRUNCATED -2
#define JMEVM_CLASSFILE_ERR_FORMAT -3
#define JMEVM_CLASSFILE_ERR_NOMEM -4
#define JMEVM_CLASSFILE_ERR_NO_MAIN -5

/* Constant pool tags (JVM spec). */
#define CONSTANT_Utf8 1
#define CONSTANT_Integer 3
#define CONSTANT_Float 4
#define CONSTANT_Long 5
#define CONSTANT_Double 6
#define CONSTANT_Class 7
#define CONSTANT_String 8
#define CONSTANT_Fieldref 9
#define CONSTANT_Methodref 10
#define CONSTANT_InterfaceMethodref 11
#define CONSTANT_NameAndType 12
#define CONSTANT_MethodHandle 15
#define CONSTANT_MethodType 16
#define CONSTANT_Dynamic 17
#define CONSTANT_InvokeDynamic 18
#define CONSTANT_Module 19
#define CONSTANT_Package 20

typedef struct cf_reader {
    const uint8_t *buf;
    size_t len;
    size_t pos;
} cf_reader;

static int cf_read_u1(cf_reader *r, uint8_t *out)
{
    if (r == NULL || out == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (r->pos + 1 > r->len) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    *out = r->buf[r->pos++];
    return JMEVM_CLASSFILE_OK;
}

static int cf_read_u2(cf_reader *r, uint16_t *out)
{
    if (r == NULL || out == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (r->pos + 2 > r->len) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    uint16_t v = (uint16_t)r->buf[r->pos] << 8;
    v |= (uint16_t)r->buf[r->pos + 1];
    r->pos += 2;
    *out = v;
    return JMEVM_CLASSFILE_OK;
}

static int cf_read_u4(cf_reader *r, uint32_t *out)
{
    if (r == NULL || out == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (r->pos + 4 > r->len) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    uint32_t v = (uint32_t)r->buf[r->pos] << 24;
    v |= (uint32_t)r->buf[r->pos + 1] << 16;
    v |= (uint32_t)r->buf[r->pos + 2] << 8;
    v |= (uint32_t)r->buf[r->pos + 3];
    r->pos += 4;
    *out = v;
    return JMEVM_CLASSFILE_OK;
}

static int cf_skip(cf_reader *r, size_t n)
{
    if (r == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (r->pos + n > r->len) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    r->pos += n;
    return JMEVM_CLASSFILE_OK;
}

static int cf_utf8_equals(
    const struct jmevm_classfile *cf,
    uint16_t cp_index,
    const char *s)
{
    if (cf == NULL || s == NULL) {
        return 0;
    }
    if (cp_index == 0 || cp_index >= cf->cp_count) {
        return 0;
    }
    if (cf->cp_tag == NULL || cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL) {
        return 0;
    }
    if (cf->cp_tag[cp_index] != CONSTANT_Utf8) {
        return 0;
    }

    size_t want_len = strlen(s);
    if (cf->cp_utf8_len[cp_index] != (uint16_t)want_len) {
        return 0;
    }

    const uint8_t *p = cf->buf + cf->cp_utf8_off[cp_index];
    return memcmp(p, s, want_len) == 0;
}

static int parse_code_attribute(
    struct jmevm_classfile *cf,
    cf_reader *r,
    size_t attribute_length)
{
    /* Code_attribute_length covers everything starting at max_stack. */
    if (r == NULL || cf == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (attribute_length > (r->len - r->pos)) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    size_t code_attr_end = r->pos + attribute_length;

    /* Constrain parsing strictly to the Code attribute range. */
    cf_reader sub = *r;
    sub.len = code_attr_end;
    sub.pos = r->pos;

    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    uint32_t code_length = 0;

    int rc = cf_read_u2(&sub, &max_stack);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }
    rc = cf_read_u2(&sub, &max_locals);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }
    rc = cf_read_u4(&sub, &code_length);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }

    if ((size_t)code_length > code_attr_end - sub.pos) {
        return JMEVM_CLASSFILE_ERR_TRUNCATED;
    }
    cf->main_code = cf->buf + sub.pos;
    cf->main_code_len = (size_t)code_length;
    cf->main_max_stack = max_stack;
    cf->main_max_locals = max_locals;
    cf->has_main = 1;

    rc = cf_skip(&sub, (size_t)code_length);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }

    uint16_t exception_table_length = 0;
    rc = cf_read_u2(&sub, &exception_table_length);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }

    size_t ex_skip = (size_t)exception_table_length * 8u;
    rc = cf_skip(&sub, ex_skip);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }

    uint16_t attributes_count = 0;
    rc = cf_read_u2(&sub, &attributes_count);
    if (rc != JMEVM_CLASSFILE_OK) {
        return rc;
    }

    for (uint16_t i = 0; i < attributes_count; i++) {
        uint16_t attr_name_index = 0;
        uint32_t attr_len = 0;
        (void)attr_name_index;
        rc = cf_read_u2(&sub, &attr_name_index);
        if (rc != JMEVM_CLASSFILE_OK) {
            return rc;
        }
        rc = cf_read_u4(&sub, &attr_len);
        if (rc != JMEVM_CLASSFILE_OK) {
            return rc;
        }
        (void)attr_name_index;
        rc = cf_skip(&sub, (size_t)attr_len);
        if (rc != JMEVM_CLASSFILE_OK) {
            return rc;
        }
    }

    /* If the lengths don't line up perfectly, skip any trailing bytes. */
    if (sub.pos < code_attr_end) {
        rc = cf_skip(&sub, code_attr_end - sub.pos);
        if (rc != JMEVM_CLASSFILE_OK) {
            return rc;
        }
    }
    r->pos = sub.pos;
    return (sub.pos == code_attr_end) ? JMEVM_CLASSFILE_OK : JMEVM_CLASSFILE_ERR_FORMAT;
}

jmevm_classfile *jmevm_classfile_load_from_buffer(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < 10) {
        return NULL;
    }

    struct jmevm_classfile *cf = (struct jmevm_classfile *)calloc(1, sizeof(*cf));
    if (cf == NULL) {
        return NULL;
    }
    cf->buf = buf;
    cf->len = len;

    cf_reader r;
    r.buf = buf;
    r.len = len;
    r.pos = 0;

    uint32_t magic = 0;
    uint16_t minor = 0;
    uint16_t major = 0;

    int rc = cf_read_u4(&r, &magic);
    if (rc != JMEVM_CLASSFILE_OK || magic != 0xCAFEBABE) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    /* Version is read but ignored. */
    rc = cf_read_u2(&r, &minor);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    rc = cf_read_u2(&r, &major);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    (void)minor;
    (void)major;

    uint16_t cp_count = 0;
    rc = cf_read_u2(&r, &cp_count);
    if (rc != JMEVM_CLASSFILE_OK || cp_count < 1) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    cf->cp_count = cp_count;

    cf->cp_tag = (uint8_t *)calloc(cp_count, sizeof(uint8_t));
    cf->cp_utf8_off = (uint32_t *)calloc(cp_count, sizeof(uint32_t));
    cf->cp_utf8_len = (uint16_t *)calloc(cp_count, sizeof(uint16_t));
    if (cf->cp_tag == NULL || cf->cp_utf8_off == NULL || cf->cp_utf8_len == NULL) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    for (uint16_t i = 1; i < cp_count; i++) {
        uint8_t tag = 0;
        rc = cf_read_u1(&r, &tag);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }
        cf->cp_tag[i] = tag;

        switch (tag) {
        case CONSTANT_Utf8: {
            uint16_t slen = 0;
            rc = cf_read_u2(&r, &slen);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            if (r.pos + (size_t)slen > r.len) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            cf->cp_utf8_off[i] = (uint32_t)r.pos;
            cf->cp_utf8_len[i] = slen;
            rc = cf_skip(&r, (size_t)slen);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;
        }
        case CONSTANT_Class:
        case CONSTANT_String:
            rc = cf_skip(&r, 2);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        case CONSTANT_NameAndType:
            rc = cf_skip(&r, 4);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        case CONSTANT_Methodref:
        case CONSTANT_Fieldref:
        case CONSTANT_InterfaceMethodref:
        case CONSTANT_Dynamic:
        case CONSTANT_InvokeDynamic:
            rc = cf_skip(&r, 4);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        case CONSTANT_MethodType:
            /* u2 descriptor_index */
            rc = cf_skip(&r, 2);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        case CONSTANT_MethodHandle: {
            /* u1 reference_kind + u2 reference_index */
            rc = cf_skip(&r, 3);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;
        }

        case CONSTANT_Integer:
        case CONSTANT_Float:
            rc = cf_skip(&r, 4);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        case CONSTANT_Long:
        case CONSTANT_Double:
            rc = cf_skip(&r, 8);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            /* These take up two constant pool entries. */
            if (i + 1 >= cp_count) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            i++;
            break;

        case CONSTANT_Module:
        case CONSTANT_Package:
            rc = cf_skip(&r, 2);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            break;

        default:
            /* For minimal viability, fail gracefully if we can't parse the constant pool. */
            jmevm_classfile_destroy(cf);
            return NULL;
        }
    }

    /* access_flags, this_class, super_class */
    rc = cf_skip(&r, 2);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    rc = cf_skip(&r, 2);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    rc = cf_skip(&r, 2);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    /* interfaces */
    uint16_t interfaces_count = 0;
    rc = cf_read_u2(&r, &interfaces_count);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    rc = cf_skip(&r, (size_t)interfaces_count * 2u);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    /* fields */
    uint16_t fields_count = 0;
    rc = cf_read_u2(&r, &fields_count);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }
    for (uint16_t i = 0; i < fields_count; i++) {
        /* access_flags, name_index, descriptor_index */
        rc = cf_skip(&r, 6);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }
        uint16_t attributes_count = 0;
        rc = cf_read_u2(&r, &attributes_count);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }
        for (uint16_t j = 0; j < attributes_count; j++) {
            uint16_t attribute_name_index = 0;
            uint32_t attribute_length = 0;
            (void)attribute_name_index;
            rc = cf_read_u2(&r, &attribute_name_index);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            rc = cf_read_u4(&r, &attribute_length);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            rc = cf_skip(&r, (size_t)attribute_length);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
        }
    }

    /* methods */
    uint16_t methods_count = 0;
    rc = cf_read_u2(&r, &methods_count);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    static const char main_name[] = "main";
    static const char main_desc[] = "([Ljava/lang/String;)V";
    static const char code_attr_name[] = "Code";

    for (uint16_t i = 0; i < methods_count; i++) {
        /* access_flags */
        rc = cf_skip(&r, 2);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }

        uint16_t name_index = 0;
        uint16_t descriptor_index = 0;
        rc = cf_read_u2(&r, &name_index);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }
        rc = cf_read_u2(&r, &descriptor_index);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }

        uint16_t attributes_count = 0;
        rc = cf_read_u2(&r, &attributes_count);
        if (rc != JMEVM_CLASSFILE_OK) {
            jmevm_classfile_destroy(cf);
            return NULL;
        }

        int is_main = cf_utf8_equals(cf, name_index, main_name) &&
                      cf_utf8_equals(cf, descriptor_index, main_desc);

        for (uint16_t j = 0; j < attributes_count; j++) {
            uint16_t attribute_name_index = 0;
            uint32_t attribute_length = 0;

            rc = cf_read_u2(&r, &attribute_name_index);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }
            rc = cf_read_u4(&r, &attribute_length);
            if (rc != JMEVM_CLASSFILE_OK) {
                jmevm_classfile_destroy(cf);
                return NULL;
            }

            if (is_main && cf_utf8_equals(cf, attribute_name_index, code_attr_name) && cf->main_code == NULL) {
                rc = parse_code_attribute(cf, &r, (size_t)attribute_length);
                if (rc != JMEVM_CLASSFILE_OK) {
                    jmevm_classfile_destroy(cf);
                    return NULL;
                }
            } else {
                rc = cf_skip(&r, (size_t)attribute_length);
                if (rc != JMEVM_CLASSFILE_OK) {
                    jmevm_classfile_destroy(cf);
                    return NULL;
                }
            }
        }
    }

    if (!cf->has_main || cf->main_code == NULL || cf->main_code_len == 0) {
        /* Keep parsing failures silent; return NULL so callers can fail gracefully. */
        jmevm_classfile_destroy(cf);
        return NULL;
    }

    return cf;
}

void jmevm_classfile_destroy(jmevm_classfile *cf)
{
    if (cf == NULL) {
        return;
    }

    free(cf->cp_tag);
    free(cf->cp_utf8_off);
    free(cf->cp_utf8_len);
    free(cf);
}

int jmevm_classfile_extract_main(const jmevm_classfile *cf, jmevm_main_method *out)
{
    if (cf == NULL || out == NULL) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }
    if (!cf->has_main || cf->main_code == NULL || cf->main_code_len == 0) {
        return JMEVM_CLASSFILE_ERR_NO_MAIN;
    }

    out->code = cf->main_code;
    out->code_len = cf->main_code_len;
    out->max_stack = cf->main_max_stack;
    out->max_locals = cf->main_max_locals;
    return JMEVM_CLASSFILE_OK;
}

int jmevm_classfile_execute_main(jmevm_vm *vm, const uint8_t *buf, size_t len)
{
    if (vm == NULL || buf == NULL || len == 0) {
        return JMEVM_CLASSFILE_ERR_BAD_ARGS;
    }

    struct jmevm_classfile *cf = jmevm_classfile_load_from_buffer(buf, len);
    if (cf == NULL) {
        return JMEVM_CLASSFILE_ERR_NO_MAIN;
    }

    jmevm_main_method m;
    int rc = jmevm_classfile_extract_main(cf, &m);
    if (rc != JMEVM_CLASSFILE_OK) {
        jmevm_classfile_destroy(cf);
        return rc;
    }

    int run_rc = jmevm_vm_run(vm, m.code, m.code_len, m.max_locals, m.max_stack);
    jmevm_classfile_destroy(cf);
    return run_rc;
}
