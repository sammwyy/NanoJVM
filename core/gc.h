#ifndef JMEVM_CORE_GC_H
#define JMEVM_CORE_GC_H

#include <stddef.h>
#include <stdint.h>

struct jmevm_vm;

/**
 * Runs the minimal mark & sweep garbage collector.
 */
void jmevm_gc_run(struct jmevm_vm *vm);

#endif
