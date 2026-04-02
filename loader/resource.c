#include "loader/resource.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */
static int str_ends_with(const char *s, const char *suf) {
  size_t sl = strlen(s), sufl = strlen(suf);
  if (sl < sufl)
    return 0;
  return memcmp(s + sl - sufl, suf, sufl) == 0;
}

static char *str_dup(const char *s) {
  size_t n = strlen(s);
  char *d = (char *)malloc(n + 1);
  if (d)
    memcpy(d, s, n + 1);
  return d;
}

/* Read a whole file into a malloc'd buffer. */
static uint8_t *file_read_all(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_len = (size_t)sz;
  return buf;
}

/* -------------------------------------------------------------------------
 * Classpath grow
 * ---------------------------------------------------------------------- */
static int cp_grow(jvm_classpath *cp) {
  if (cp->count < cp->cap)
    return 0;
  size_t new_cap = cp->cap ? cp->cap * 2 : 8;
  jvm_cp_entry *e = (jvm_cp_entry *)realloc(cp->entries, new_cap * sizeof(*e));
  if (!e)
    return -1;
  cp->entries = e;
  cp->cap = new_cap;
  return 0;
}

static int cp_add_raw(jvm_classpath *cp, jvm_cp_kind kind, const char *path,
                      jvm_jar *jar) {
  if (cp_grow(cp) != 0)
    return -1;
  jvm_cp_entry *e = &cp->entries[cp->count++];
  e->kind = kind;
  e->path = str_dup(path);
  e->jar = jar;
  if (!e->path) {
    cp->count--;
    return -1;
  }
  return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
jvm_classpath *jvm_classpath_create(void) {
  return (jvm_classpath *)calloc(1, sizeof(jvm_classpath));
}

void jvm_classpath_destroy(jvm_classpath *cp) {
  if (!cp)
    return;
  for (size_t i = 0; i < cp->count; i++) {
    free(cp->entries[i].path);
    if (cp->entries[i].jar)
      jvm_jar_close(cp->entries[i].jar);
  }
  free(cp->entries);
  free(cp);
}

int jvm_classpath_add(jvm_classpath *cp, const char *entry) {
  if (!cp || !entry || entry[0] == '\0')
    return 0;

  size_t elen = strlen(entry);

  /* Wildcard: "some/dir/*" - add all .jar in that dir */
  if (elen >= 2 && entry[elen - 1] == '*') {
    char *dir = str_dup(entry);
    if (!dir)
      return -1;
    dir[elen - 1] = '\0'; /* remove '*' */
    /* strip trailing slash or nothing */
    if (elen >= 3 && (dir[elen - 2] == '/' || dir[elen - 2] == '\\'))
      dir[elen - 2] = '\0';

    DIR *d = opendir(*dir ? dir : ".");
    if (!d) {
      free(dir);
      return 0;
    } /* silently skip missing dirs */
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
      if (!str_ends_with(de->d_name, ".jar"))
        continue;
      /* build full path */
      char *slash = (*dir) ? "/" : "";
      size_t plen = strlen(dir) + 1 + strlen(de->d_name) + 1;
      char *full = (char *)malloc(plen);
      if (!full)
        continue;
      snprintf(full, plen, "%s%s%s", dir, slash, de->d_name);
      jvm_jar *jar = jvm_jar_open(full);
      if (jar)
        cp_add_raw(cp, JVM_CP_JAR, full, jar);
      free(full);
    }
    closedir(d);
    free(dir);
    return 0;
  }

  /* .jar or .zip file */
  if (str_ends_with(entry, ".jar") || str_ends_with(entry, ".zip")) {
    jvm_jar *jar = jvm_jar_open(entry);
    if (!jar) {
      fprintf(stderr, "[classpath] warning: could not open JAR: %s\n", entry);
      return 0;
    }
    return cp_add_raw(cp, JVM_CP_JAR, entry, jar);
  }

  /* Single .class file */
  if (str_ends_with(entry, ".class")) {
    return cp_add_raw(cp, JVM_CP_CLASS, entry, NULL);
  }

  /* Otherwise treat as a directory */
  return cp_add_raw(cp, JVM_CP_DIR, entry, NULL);
}

int jvm_classpath_add_path(jvm_classpath *cp, const char *path_str) {
  if (!cp || !path_str)
    return 0;
  char *dup = str_dup(path_str);
  if (!dup)
    return -1;
  char *tok = strtok(dup, ":;");
  while (tok) {
    jvm_classpath_add(cp, tok);
    tok = strtok(NULL, ":;");
  }
  free(dup);
  return 0;
}

/* -------------------------------------------------------------------------
 * Extract the JVM internal class name from a .class file path.
 * e.g. "./com/example/Foo.class" → "com/example/Foo"
 * ---------------------------------------------------------------------- */
static char *class_name_from_path(const char *path) {
  /* strip leading "./" */
  while (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
    path += 2;
  size_t len = strlen(path);
  if (len < 6)
    return NULL;
  char *name = str_dup(path);
  if (!name)
    return NULL;
  /* strip .class suffix */
  name[len - 6] = '\0';
  /* normalise backslashes */
  for (char *p = name; *p; p++)
    if (*p == '\\')
      *p = '/';
  return name;
}

uint8_t *jvm_classpath_find_class(const jvm_classpath *cp,
                                  const char *class_name, size_t *out_len) {
  if (!cp || !class_name || !out_len)
    return NULL;

  for (size_t i = 0; i < cp->count; i++) {
    const jvm_cp_entry *e = &cp->entries[i];

    if (e->kind == JVM_CP_JAR && e->jar) {
      uint8_t *buf = jvm_jar_read_class(e->jar, class_name, out_len);
      if (buf)
        return buf;
      continue;
    }

    if (e->kind == JVM_CP_DIR) {
      /* Build <dir>/<class_name>.class */
      size_t dlen = strlen(e->path);
      size_t nlen = strlen(class_name);
      char *full = (char *)malloc(dlen + 1 + nlen + 7);
      if (!full)
        continue;
      snprintf(full, dlen + 1 + nlen + 7, "%s/%s.class", e->path, class_name);
      uint8_t *buf = file_read_all(full, out_len);
      free(full);
      if (buf)
        return buf;
      continue;
    }

    if (e->kind == JVM_CP_CLASS) {
      /* Single class: check if the name matches */
      char *embedded_name = class_name_from_path(e->path);
      if (!embedded_name)
        continue;
      int match = (strcmp(embedded_name, class_name) == 0);
      free(embedded_name);
      if (match) {
        return file_read_all(e->path, out_len);
      }
    }
  }
  return NULL;
}
