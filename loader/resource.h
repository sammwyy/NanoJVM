#ifndef JVM_LOADER_RESOURCE_H
#define JVM_LOADER_RESOURCE_H

#include "loader/jar.h"
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * jvm_classpath — ordered list of classpath entries.
 *
 * Each entry is one of:
 *   - A directory  (search for <ClassName>.class inside)
 *   - A .jar file  (search via the JAR index)
 *   - A single .class file  (loaded once, available as its embedded name)
 *
 * Mirrors the JVM -cp semantics:
 *   nanojvm -cp dir:some.jar:Other.class MainClass
 * ------------------------------------------------------------------------- */

typedef enum {
  JVM_CP_DIR,   /* a directory                   */
  JVM_CP_JAR,   /* a JAR/zip file                */
  JVM_CP_CLASS, /* a single .class file          */
} jvm_cp_kind;

typedef struct jvm_cp_entry {
  jvm_cp_kind kind;
  char *path;   /* heap-owned                           */
  jvm_jar *jar; /* non-NULL when kind == JVM_CP_JAR   */
} jvm_cp_entry;

typedef struct jvm_classpath {
  jvm_cp_entry *entries;
  size_t count;
  size_t cap;
} jvm_classpath;

/* Create an empty classpath. */
jvm_classpath *jvm_classpath_create(void);

/* Free all entries and the classpath itself. */
void jvm_classpath_destroy(jvm_classpath *cp);

/* Add a single string entry (auto-detect dir / .jar / .class).
 * Glob suffix `*` expands all .jar files in a directory.
 * Returns 0 on success. */
int jvm_classpath_add(jvm_classpath *cp, const char *entry);

/* Parse a colon/semicolon-separated classpath string. */
int jvm_classpath_add_path(jvm_classpath *cp, const char *path_str);

/* Find and load a class by JVM internal name (e.g. "com/example/Foo").
 * Searches entries in order.  Returns heap-allocated .class bytes or NULL.
 * *out_len set to byte count. */
uint8_t *jvm_classpath_find_class(const jvm_classpath *cp,
                                  const char *class_name, size_t *out_len);

#endif /* JVM_LOADER_RESOURCE_H */
