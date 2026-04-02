#ifndef JMEVM_CORE_HEAP_H
#define JMEVM_CORE_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct jmevm_classfile;

/**
 * Minimal object representation.
 */
typedef struct jmevm_object {
  const struct jmevm_classfile *cf;
  int32_t *fields;
} jmevm_object;

/**
 * Initializes the heap with a fixed size.
 */
void jmevm_heap_init(size_t size);

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
 * Gets the classfile for the given object reference.
 */
const struct jmevm_classfile *jmevm_heap_get_classfile(int32_t obj_ref);

#endif
