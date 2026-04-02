#include "core/heap.h"
#include "core/classfile.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECTS 1024

static jmevm_object objects[MAX_OBJECTS];
static int heap_initialized = 0;

void jmevm_heap_init(size_t size) {
  (void)size;
  for (int i = 0; i < MAX_OBJECTS; i++) {
    objects[i].type = JMEVM_OBJ_FREE;
  }
  heap_initialized = 1;
}

int jmevm_heap_is_initialized(void) { return heap_initialized; }

jmevm_object *jmevm_heap_get_objects(void) { return objects; }
uint16_t jmevm_heap_get_max_objects(void) { return MAX_OBJECTS; }

int32_t jmevm_heap_alloc(const struct jmevm_classfile *cf) {
  uint16_t obj_ref = 0;
  for (uint16_t i = 1; i < MAX_OBJECTS; i++) {
    if (objects[i].type == JMEVM_OBJ_FREE) {
      obj_ref = i;
      break;
    }
  }

  if (obj_ref == 0) {
    // Heap full: return 0, VM might trigger GC and retry or just fail.
    return 0;
  }

  uint16_t fields_count = cf ? cf->fields_count : 2;
  jmevm_object *obj = &objects[obj_ref];
  obj->type = JMEVM_OBJ_CLASS;
  obj->cf = cf;
  obj->marked = 0;
  obj->fields = calloc(fields_count, sizeof(int32_t));
  obj->length = 0;
  obj->data = NULL;

  return (int32_t)obj_ref;
}

int32_t jmevm_heap_alloc_array(jmevm_obj_type type, int32_t length) {
  uint16_t obj_ref = 0;
  for (uint16_t i = 1; i < MAX_OBJECTS; i++) {
    if (objects[i].type == JMEVM_OBJ_FREE) {
      obj_ref = i;
      break;
    }
  }

  if (obj_ref == 0) {
    return 0;
  }

  jmevm_object *obj = &objects[obj_ref];
  obj->type = type;
  obj->cf = NULL;
  obj->marked = 0;
  obj->fields = NULL;
  obj->length = length;

  size_t elem_size =
      (type == JMEVM_OBJ_ARRAY_INT || type == JMEVM_OBJ_ARRAY_OBJ)
          ? sizeof(int32_t)
          : 1;
  obj->data = calloc((size_t)length, elem_size);

  return (int32_t)obj_ref;
}

int32_t jmevm_array_length(int32_t array_ref) {
  if (array_ref <= 0 || array_ref >= MAX_OBJECTS)
    return 0;
  if (objects[array_ref].type == JMEVM_OBJ_FREE)
    return 0;
  return objects[array_ref].length;
}

int32_t jmevm_array_load_int(int32_t array_ref, int32_t index) {
  if (array_ref <= 0 || array_ref >= MAX_OBJECTS)
    return 0;
  jmevm_object *obj = &objects[array_ref];
  if ((obj->type != JMEVM_OBJ_ARRAY_INT && obj->type != JMEVM_OBJ_ARRAY_OBJ) ||
      index < 0 || index >= obj->length)
    return 0;
  return ((int32_t *)obj->data)[index];
}

void jmevm_array_store_int(int32_t array_ref, int32_t index, int32_t value) {
  if (array_ref <= 0 || array_ref >= MAX_OBJECTS)
    return;
  jmevm_object *obj = &objects[array_ref];
  if ((obj->type != JMEVM_OBJ_ARRAY_INT && obj->type != JMEVM_OBJ_ARRAY_OBJ) ||
      index < 0 || index >= obj->length)
    return;
  ((int32_t *)obj->data)[index] = value;
}

int8_t jmevm_array_load_byte(int32_t array_ref, int32_t index) {
  if (array_ref <= 0 || array_ref >= MAX_OBJECTS)
    return 0;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_BYTE || index < 0 || index >= obj->length)
    return 0;
  return ((int8_t *)obj->data)[index];
}

void jmevm_array_store_byte(int32_t array_ref, int32_t index, int8_t value) {
  if (array_ref <= 0 || array_ref >= MAX_OBJECTS)
    return;
  jmevm_object *obj = &objects[array_ref];
  if (obj->type != JMEVM_OBJ_ARRAY_BYTE || index < 0 || index >= obj->length)
    return;
  ((int8_t *)obj->data)[index] = value;
}

int32_t jmevm_object_get_field(int32_t obj_ref, uint16_t field_index) {
  if (obj_ref <= 0 || obj_ref >= MAX_OBJECTS) {
    return 0;
  }

  jmevm_object *obj = &objects[obj_ref];
  if (obj->type == JMEVM_OBJ_FREE)
    return 0;

  uint16_t max_fields = obj->cf ? obj->cf->fields_count : 2;
  if (field_index >= max_fields || obj->fields == NULL) {
    return 0;
  }

  return obj->fields[field_index];
}

void jmevm_object_put_field(int32_t obj_ref, uint16_t field_index,
                            int32_t value) {
  if (obj_ref <= 0 || obj_ref >= MAX_OBJECTS) {
    return;
  }

  jmevm_object *obj = &objects[obj_ref];
  if (obj->type == JMEVM_OBJ_FREE)
    return;

  uint16_t max_fields = obj->cf ? obj->cf->fields_count : 2;
  if (field_index >= max_fields || obj->fields == NULL) {
    return;
  }

  obj->fields[field_index] = value;
}

const struct jmevm_classfile *jmevm_heap_get_classfile(int32_t obj_ref) {
  if (obj_ref <= 0 || obj_ref >= MAX_OBJECTS)
    return NULL;
  if (objects[obj_ref].type == JMEVM_OBJ_FREE)
    return NULL;
  return objects[obj_ref].cf;
}
