#ifndef JVM_CORE_HEAP_H
#define JVM_CORE_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct jvm_classfile;

/**
 * Minimal object representation.
 */
typedef enum {
  JVM_OBJ_FREE = -1,
  JVM_OBJ_CLASS = 0,
  JVM_OBJ_ARRAY_INT,
  JVM_OBJ_ARRAY_BYTE,
  JVM_OBJ_ARRAY_OBJ,
} jvm_obj_type;

/**
 * Minimal object representation.
 */
typedef struct jvm_object {
  jvm_obj_type type;
  const struct jvm_classfile *cf;
  int32_t *fields;
  int32_t length;
  void *data;
  uint8_t marked;
} jvm_object;

/**
 * Initializes the heap with a fixed size.
 */
void jvm_heap_init(size_t size);
int jvm_heap_is_initialized(void);

/**
 * Allocates a new object of the given class.
 */
int32_t jvm_heap_alloc(const struct jvm_classfile *cf);

/**
 * Accesses an object field.
 */
int32_t jvm_object_get_field(int32_t obj_ref, uint16_t field_index);
void jvm_object_put_field(int32_t obj_ref, uint16_t field_index, int32_t value);

/**
 * Allocates a new array on the heap.
 */
int32_t jvm_heap_alloc_array(jvm_obj_type type, int32_t length);

/**
 * Gets the length of an array.
 */
int32_t jvm_array_length(int32_t array_ref);

/**
 * Array access routines.
 */
int32_t jvm_array_load_int(int32_t array_ref, int32_t index);
void jvm_array_store_int(int32_t array_ref, int32_t index, int32_t value);
int8_t jvm_array_load_byte(int32_t array_ref, int32_t index);
void jvm_array_store_byte(int32_t array_ref, int32_t index, int8_t value);

/**
 * Gets the classfile for the given object reference.
 */
const struct jvm_classfile *jvm_heap_get_classfile(int32_t obj_ref);

#endif
