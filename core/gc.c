#include "core/gc.h"
#include "core/classfile.h"
#include "core/heap.h"
#include "core/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern jmevm_object *jmevm_heap_get_objects(void);
extern uint16_t jmevm_heap_get_max_objects(void);

static void mark_object(struct jmevm_vm *vm, int32_t ref);

static void mark_value(struct jmevm_vm *vm, int32_t val) {
  if (val <= 0)
    return;
  mark_object(vm, val);
}

static void mark_object(struct jmevm_vm *vm, int32_t ref) {
  uint16_t max_objs = jmevm_heap_get_max_objects();
  if (ref <= 0 || ref >= max_objs)
    return;

  jmevm_object *objs = jmevm_heap_get_objects();
  jmevm_object *obj = &objs[ref];

  if (obj->type == JMEVM_OBJ_FREE || obj->marked)
    return;

  obj->marked = 1;

  // Follow references
  if (obj->type == JMEVM_OBJ_CLASS) {
    uint16_t fields_count = obj->cf ? obj->cf->fields_count : 2;
    for (uint16_t i = 0; i < fields_count; i++) {
      mark_value(vm, obj->fields[i]);
    }
  } else if (obj->type == JMEVM_OBJ_ARRAY_OBJ) {
    int32_t *data = (int32_t *)obj->data;
    for (int32_t i = 0; i < obj->length; i++) {
      mark_value(vm, data[i]);
    }
  }
}

void jmevm_gc_run(struct jmevm_vm *vm) {
  jmevm_object *objs = jmevm_heap_get_objects();
  uint16_t max_objs = jmevm_heap_get_max_objects();

  // 1. Reset mark flags
  for (uint16_t i = 0; i < max_objs; i++) {
    objs[i].marked = 0;
  }

  // 2. Mark roots
  for (uint16_t fidx = 0; fidx < vm->frame_top; fidx++) {
    jmevm_frame *f = &vm->frames[fidx];
    for (uint16_t i = 0; i < f->max_locals; i++) {
      mark_value(vm, f->locals[i]);
    }
    for (uint16_t i = 0; i < f->sp; i++) {
      mark_value(vm, f->stack[i]);
    }
  }

  if (vm->exception_obj != 0) {
    mark_value(vm, vm->exception_obj);
  }

  // 3. Sweep
  size_t reclaimed_count = 0;
  for (uint16_t i = 1; i < max_objs; i++) {
    if (objs[i].type != JMEVM_OBJ_FREE && !objs[i].marked) {
      // Reclaim
      if (objs[i].fields) {
        free(objs[i].fields);
        objs[i].fields = NULL;
      }
      if (objs[i].data) {
        free(objs[i].data);
        objs[i].data = NULL;
      }
      objs[i].type = JMEVM_OBJ_FREE;
      objs[i].cf = NULL;
      reclaimed_count++;
    }
  }

  if (reclaimed_count > 0) {
    // printf("[GC] Reclaimed %zu objects\n", reclaimed_count);
  }
}
