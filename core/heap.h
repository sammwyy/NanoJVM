#ifndef JMEVM_CORE_HEAP_H
#define JMEVM_CORE_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct jmevm_classfile;

/**
 * Minimal object representation.
 */
typedef enum {
  JMEVM_OBJ_FREE = -1,
  JMEVM_OBJ_CLASS = 0,
  JMEVM_OBJ_ARRAY_INT,
  JMEVM_OBJ_ARRAY_BYTE,
  JMEVM_OBJ_ARRAY_OBJ,
} jmevm_obj_type;

/**
 * Minimal object representation.
 */
typedef struct jmevm_object {
  jmevm_obj_type type;
  const struct jmevm_classfile *cf;
  int32_t *fields;
  int32_t length;
  void *data;
  uint8_t marked;
} jmevm_object;

/**
 * Initializes the heap with a fixed size.
 */
void jmevm_heap_init(size_t size);
int jmevm_heap_is_initialized(void);

/**
 * Allocates a new object of the given class.
 */
int32_t jmevm_heap_alloc(const struct jmevm_classfile *cf);

/**
 * Accesses an object field.
 */
int32_t jmevm_object_get_field(int32_t obj_ref, uint16_t field_index);
void jmevm_object_put_field(int32_t obj_ref, uint16_t field_index,
                            int32_t value);

/**
 * Allocates a new array on the heap.
 */
int32_t jmevm_heap_alloc_array(jmevm_obj_type type, int32_t length);

/**
 * Gets the length of an array.
 */
int32_t jmevm_array_length(int32_t array_ref);

/**
 * Array access routines.
 */
int32_t jmevm_array_load_int(int32_t array_ref, int32_t index);
void jmevm_array_store_int(int32_t array_ref, int32_t index, int32_t value);
int8_t jmevm_array_load_byte(int32_t array_ref, int32_t index);
void jmevm_array_store_byte(int32_t array_ref, int32_t index, int8_t value);

/**
 * Gets the classfile for the given object reference.
 */
const struct jmevm_classfile *jmevm_heap_get_classfile(int32_t obj_ref);

#endif
