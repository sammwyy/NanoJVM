#ifndef JVM_LOADER_JAR_H
#define JVM_LOADER_JAR_H

#include "loader/zip.h"
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * jvm_jar — a JAR file (which is a ZIP) with helpers specific to the VM.
 *
 * A JAR may contain:
 *   - .class files  (e.g. com/example/Main.class)
 *   - META-INF/MANIFEST.MF  (used to read Main-Class for -jar mode)
 *   - resources (arbitrary bytes)
 * ------------------------------------------------------------------------- */

typedef struct jvm_jar {
  uint8_t *buf; /* heap-owned copy of the file              */
  size_t len;
  jvm_zip *zip;     /* parsed index into buf                    */
  char *main_class; /* from manifest, or NULL               */
} jvm_jar;

/* Load a JAR from a file path.  Returns NULL on error. */
jvm_jar *jvm_jar_open(const char *path);

/* Free everything (buf, zip index, main_class). */
void jvm_jar_close(jvm_jar *jar);

/* Number of entries in the JAR. */
size_t jvm_jar_entry_count(const jvm_jar *jar);

/* Find a .class entry by its JVM internal name, e.g. "com/example/Foo"
 * (without the .class suffix).  Returns entry index or -1. */
int jvm_jar_find_class(const jvm_jar *jar, const char *class_name);

/* Extract any entry by index. Caller must free() the result. */
uint8_t *jvm_jar_read_entry(const jvm_jar *jar, size_t idx, size_t *out_len);

/* Extract a .class by JVM internal name.  Caller must free(). */
uint8_t *jvm_jar_read_class(const jvm_jar *jar, const char *class_name,
                            size_t *out_len);

#endif /* JVM_LOADER_JAR_H */
