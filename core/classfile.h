#ifndef JVM_CORE_CLASSFILE_H
#define JVM_CORE_CLASSFILE_H

#include "nanojvm.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal internal representation of a class file for this project:
 * enough to locate and extract the Code attribute for a single method:
 *   name: "main"
 *   descriptor: "([Ljava/lang/String;)V"
 */
struct jvm_exception_handler {
  uint16_t start_pc;
  uint16_t end_pc;
  uint16_t handler_pc;
  uint16_t catch_type; /* 0 for any exception, else constant-pool index */
};

struct jvm_method {
  uint16_t name_cp_index;       /* CONSTANT_Utf8 */
  uint16_t descriptor_cp_index; /* CONSTANT_Utf8 */

  const uint8_t *code;
  size_t code_len;
  uint16_t max_stack;
  uint16_t max_locals;

  uint16_t exception_table_length;
  struct jvm_exception_handler *exception_table;
};

struct jvm_field {
  uint16_t name_cp_index;       /* CONSTANT_Utf8 */
  uint16_t descriptor_cp_index; /* CONSTANT_Utf8 */
};

struct jvm_classfile {
  const uint8_t *buf;
  size_t len;

  uint16_t cp_count;
  uint8_t *cp_tag;       /* [cp_count] */
  uint32_t *cp_utf8_off; /* [cp_count], valid only for CONSTANT_Utf8 */
  uint16_t *cp_utf8_len; /* [cp_count], valid only for CONSTANT_Utf8 */

  /* Minimal extra constant-pool data to resolve Methodref for invokestatic. */
  uint16_t *cp_class_name_index; /* [cp_count] valid for CONSTANT_Class */
  uint16_t *cp_nat_name_index;   /* [cp_count] valid for CONSTANT_NameAndType */
  uint16_t *cp_nat_desc_index;   /* [cp_count] valid for CONSTANT_NameAndType */
  uint16_t
      *cp_methodref_class_index; /* [cp_count] valid for CONSTANT_Methodref */
  uint16_t
      *cp_methodref_nat_index; /* [cp_count] valid for CONSTANT_Methodref */
  uint16_t
      *cp_fieldref_class_index;    /* [cp_count] valid for CONSTANT_Fieldref */
  uint16_t *cp_fieldref_nat_index; /* [cp_count] valid for CONSTANT_Fieldref */
  uint16_t *cp_string_index;       /* [cp_count] valid for CONSTANT_String */
  int32_t *cp_integer;             /* [cp_count] valid for CONSTANT_Integer */

  uint16_t
      this_class_name_cp_index; /* CONSTANT_Utf8 index for this class, or 0 */
  uint16_t
      super_class_name_cp_index; /* CONSTANT_Utf8 index for super class, or 0 */

  int has_main;
  const uint8_t *main_code;
  size_t main_code_len;
  uint16_t main_max_stack;
  uint16_t main_max_locals;

  uint16_t fields_count;
  struct jvm_field *fields;

  uint16_t methods_count;
  struct jvm_method *methods;
};

/* Lookup by constant-pool UTF8 indices (name+descriptor). */
const struct jvm_method *
jvm_classfile_lookup_method(const struct jvm_classfile *cf,
                            uint16_t name_cp_index,
                            uint16_t descriptor_cp_index);

/**
 * Resolves a method by its name and descriptor.
 */
const struct jvm_method *
jvm_classfile_resolve_method(const struct jvm_classfile *cf, const char *name,
                             const char *descriptor);

/**
 * Resolves a field index based on its name and descriptor.
 */
int jvm_classfile_resolve_field(const struct jvm_classfile *cf,
                                const char *name, const char *descriptor);

/**
 * Gets a UTF-8 string from the constant pool as a null-terminated string.
 * The caller must free the returned string.
 */
char *jvm_classfile_get_utf8_copy(const struct jvm_classfile *cf,
                                  uint16_t cp_index);

/**
 * Returns 1 if the UTF-8 entry at the given index equals the given string.
 */
int jvm_classfile_utf8_equals(const struct jvm_classfile *cf, uint16_t cp_index,
                              const char *s);

#endif /* JVM_CORE_CLASSFILE_H */
