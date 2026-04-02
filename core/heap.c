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

static int heap_initialized = 0;

void jmevm_heap_init(size_t size) {
  (void)size;
  memset(objects, 0, sizeof(objects));
  memset(field_storage, 0, sizeof(field_storage));
  next_obj = 1;
  next_field = 0;
  heap_initialized = 1;
}

int jmevm_heap_is_initialized(void) { return heap_initialized; }

int32_t jmevm_heap_alloc(const struct jmevm_classfile *cf) {
  if (next_obj >= MAX_OBJECTS) {
    fprintf(stderr, "[ERROR] Out of object slots\n");
    return 0;
  }

  uint16_t fields_count = cf ? cf->fields_count : 2; // For String/special cases
  if (next_field + fields_count > MAX_FIELDS) {
    fprintf(stderr, "[ERROR] Out of field storage\n");
    return 0;
  }

  int32_t obj_ref = (int32_t)next_obj++;
  jmevm_object *obj = &objects[obj_ref];
  obj->type = JMEVM_OBJ_CLASS;
  obj->cf = cf;
  obj->fields = &field_storage[next_field];

  // Initialize fields to 0
  for (uint16_t i = 0; i < fields_count; i++) {
    obj->fields[i] = 0;
  }

  next_field += fields_count;

  return obj_ref;
}

int32_t jmevm_heap_alloc_array(jmevm_obj_type type, int32_t length) {
  if (next_obj >= MAX_OBJECTS) {
    fprintf(stderr, "[ERROR] Out of object slots\n");
    return 0;
  }

  int32_t obj_ref = (int32_t)next_obj++;
  jmevm_object *obj = &objects[obj_ref];
  obj->type = type;
  obj->cf = NULL;
  obj->length = length;

  size_t elem_size = (type == JMEVM_OBJ_ARRAY_INT) ? sizeof(int32_t) : 1;
  obj->data = calloc((size_t)length, elem_size);

  return obj_ref;
}

int32_t jmevm_array_length(int32_t array_ref) {
  if (array_ref <= 0 || array_ref >= next_obj)
    return 0;
  return objects[array_ref].length;
}

int32_t jmevm_array_load_int(int32_t array_ref, int32_t index) {
  if (array_ref <= 0 || array_ref >= next_obj)
    return 0;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_INT || index < 0 || index >= obj->length)
    return 0;
  return ((int32_t *)obj->data)[index];
}

void jmevm_array_store_int(int32_t array_ref, int32_t index, int32_t value) {
  if (array_ref <= 0 || array_ref >= next_obj)
    return;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_INT || index < 0 || index >= obj->length)
    return;
  ((int32_t *)obj->data)[index] = value;
}

int8_t jmevm_array_load_byte(int32_t array_ref, int32_t index) {
  if (array_ref <= 0 || array_ref >= next_obj)
    return 0;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_BYTE || index < 0 || index >= obj->length)
    return 0;
  return ((int8_t *)obj->data)[index];
}

void jmevm_array_store_byte(int32_t array_ref, int32_t index, int8_t value) {
  if (array_ref <= 0 || array_ref >= next_obj)
    return;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_BYTE || index < 0 || index >= obj->length)
    return;
  ((int8_t *)obj->data)[index] = value;
}

int32_t jmevm_object_get_field(int32_t obj_ref, uint16_t field_index) {
  if (obj_ref <= 0 || obj_ref >= next_obj) {
    fprintf(stderr, "[ERROR] Invalid object reference: %d\n", obj_ref);
    return 0;
  }

  jmevm_object *obj = &objects[obj_ref];
  // String support: might not have a CF.
  uint16_t max_fields = obj->cf ? obj->cf->fields_count : 2;
  if (field_index >= max_fields) {
    fprintf(stderr, "[ERROR] Field index out of bounds: %d\n", field_index);
    return 0;
  }

  int32_t val = obj->fields[field_index];
  return val;
}

void jmevm_object_put_field(int32_t obj_ref, uint16_t field_index,
                            int32_t value) {
  if (obj_ref <= 0 || obj_ref >= next_obj) {
    fprintf(stderr, "[ERROR] Invalid object reference: %d\n", obj_ref);
    return;
  }

  jmevm_object *obj = &objects[obj_ref];
  uint16_t max_fields = obj->cf ? obj->cf->fields_count : 2;
  if (field_index >= max_fields) {
    fprintf(stderr, "[ERROR] Field index out of bounds: %d\n", field_index);
    return;
  }

  obj->fields[field_index] = value;
}

const struct jmevm_classfile *jmevm_heap_get_classfile(int32_t obj_ref) {
  if (obj_ref <= 0 || obj_ref >= next_obj)
    return NULL;
  return objects[obj_ref].cf;
}
