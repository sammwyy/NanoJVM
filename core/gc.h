#ifndef JVM_CORE_GC_H
#define JVM_CORE_GC_H

#include <stddef.h>
#include <stdint.h>

struct jvm_vm;

/**
 * Runs the minimal mark & sweep garbage collector.
 */
void jvm_gc_run(struct jvm_vm *vm);

#endif
