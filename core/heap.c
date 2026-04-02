#include "core/heap.h"
#include "core/classfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECTS 1024
#define MAX_FIELDS 4096

static jmevm_object objects[MAX_OBJECTS];
static int32_t field_storage[MAX_FIELDS];
static uint16_t next_obj = 1; // 0 is reserved for null
static uint16_t next_field = 0;

void jmevm_heap_init(size_t size) {
  (void)size;
  memset(objects, 0, sizeof(objects));
  memset(field_storage, 0, sizeof(field_storage));
  next_obj = 1;
  next_field = 0;
}

int32_t jmevm_heap_alloc(const struct jmevm_classfile *cf) {
  if (next_obj >= MAX_OBJECTS) {
    fprintf(stderr, "[ERROR] Out of object slots\n");
    return 0;
  }

  if (next_field + cf->fields_count > MAX_FIELDS) {
    fprintf(stderr, "[ERROR] Out of field storage\n");
    return 0;
  }

  int32_t obj_ref = (int32_t)next_obj++;
  jmevm_object *obj = &objects[obj_ref];
  obj->cf = cf;
  obj->fields = &field_storage[next_field];

  // Initialize fields to 0
  for (uint16_t i = 0; i < cf->fields_count; i++) {
    obj->fields[i] = 0;
  }

  next_field += cf->fields_count;

  printf("[DEBUG] Allocated object #%d of class at %p (fields: %d)\n", obj_ref,
         (void *)cf, cf->fields_count);

  return obj_ref;
}

int32_t jmevm_object_get_field(int32_t obj_ref, uint16_t field_index) {
  if (obj_ref <= 0 || obj_ref >= next_obj) {
    fprintf(stderr, "[ERROR] Invalid object reference: %d\n", obj_ref);
    return 0;
  }

  jmevm_object *obj = &objects[obj_ref];
  if (field_index >= obj->cf->fields_count) {
    fprintf(stderr, "[ERROR] Field index out of bounds: %d\n", field_index);
    return 0;
  }

  int32_t val = obj->fields[field_index];
  printf("[DEBUG] Object #%d, Get field %d -> %d\n", obj_ref, field_index, val);
  return val;
}

void jmevm_object_put_field(int32_t obj_ref, uint16_t field_index,
                            int32_t value) {
  if (obj_ref <= 0 || obj_ref >= next_obj) {
    fprintf(stderr, "[ERROR] Invalid object reference: %d\n", obj_ref);
    return;
  }

  jmevm_object *obj = &objects[obj_ref];
  if (field_index >= obj->cf->fields_count) {
    fprintf(stderr, "[ERROR] Field index out of bounds: %d\n", field_index);
    return;
  }

  obj->fields[field_index] = value;
  printf("[DEBUG] Object #%d, Put field %d <- %d\n", obj_ref, field_index,
         value);
}

const struct jmevm_classfile *jmevm_heap_get_classfile(int32_t obj_ref) {
  if (obj_ref <= 0 || obj_ref >= next_obj)
    return NULL;
  return objects[obj_ref].cf;
}
