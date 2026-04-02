#ifndef JMEVM_LOADER_RESOURCE_H
#define JMEVM_LOADER_RESOURCE_H

#include "loader/jar.h"
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * jmevm_classpath — ordered list of classpath entries.
 *
 * Each entry is one of:
 *   - A directory  (search for <ClassName>.class inside)
 *   - A .jar file  (search via the JAR index)
 *   - A single .class file  (loaded once, available as its embedded name)
 *
 * Mirrors the JVM -cp semantics:
 *   jmevm -cp dir:some.jar:Other.class MainClass
 * ------------------------------------------------------------------------- */

typedef enum {
  JMEVM_CP_DIR,   /* a directory                   */
  JMEVM_CP_JAR,   /* a JAR/zip file                */
  JMEVM_CP_CLASS, /* a single .class file          */
} jmevm_cp_kind;

typedef struct jmevm_cp_entry {
  jmevm_cp_kind kind;
  char *path;     /* heap-owned                           */
  jmevm_jar *jar; /* non-NULL when kind == JMEVM_CP_JAR   */
} jmevm_cp_entry;

typedef struct jmevm_classpath {
  jmevm_cp_entry *entries;
  size_t count;
  size_t cap;
} jmevm_classpath;

/* Create an empty classpath. */
jmevm_classpath *jmevm_classpath_create(void);

/* Free all entries and the classpath itself. */
void jmevm_classpath_destroy(jmevm_classpath *cp);

/* Add a single string entry (auto-detect dir / .jar / .class).
 * Glob suffix `*` expands all .jar files in a directory.
 * Returns 0 on success. */
int jmevm_classpath_add(jmevm_classpath *cp, const char *entry);

/* Parse a colon/semicolon-separated classpath string. */
int jmevm_classpath_add_path(jmevm_classpath *cp, const char *path_str);

/* Find and load a class by JVM internal name (e.g. "com/example/Foo").
 * Searches entries in order.  Returns heap-allocated .class bytes or NULL.
 * *out_len set to byte count. */
uint8_t *jmevm_classpath_find_class(const jmevm_classpath *cp,
                                    const char *class_name, size_t *out_len);

#endif /* JMEVM_LOADER_RESOURCE_H */
