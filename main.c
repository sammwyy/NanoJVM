#include "core/classfile.h"
#include "core/runtime.h"
#include "jmevm.h"
#include "loader/jar.h"
#include "loader/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Usage
 * ---------------------------------------------------------------------- */
static void usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s [options] <MainClass> [args...]\n"
          "  %s [options] -jar <file.jar> [args...]\n"
          "\n"
          "Options:\n"
          "  -cp <path>          Classpath (colon/semicolon-separated)\n"
          "  -classpath <path>   Same as -cp\n"
          "  -jar <file.jar>     Run the main class from a JAR manifest\n"
          "  -verbose            Print class loading info\n"
          "  -version            Print version and exit\n"
          "  -help, -h, --help   Print this help and exit\n"
          "\n"
          "Classpath entries:\n"
          "  dir/            Search directory for .class files\n"
          "  file.jar        Add a JAR to the classpath\n"
          "  dir/*           Add all .jar files in a directory\n"
          "  File.class      Add a single .class file\n"
          "\n"
          "Examples:\n"
          "  %s -cp out Main\n"
          "  %s -cp lib/* -cp out com/example/Main\n"
          "  %s -jar app.jar\n",
          prog, prog, prog, prog, prog);
}

/* -------------------------------------------------------------------------
 * Load every .class from a JAR into the VM registry
 * ---------------------------------------------------------------------- */
static int load_all_classes_from_jar(jmevm_vm *vm, jmevm_jar *jar,
                                     int verbose) {
  size_t n = jmevm_jar_entry_count(jar);
  int loaded = 0;
  for (size_t i = 0; i < n; i++) {
    jmevm_zip_entry ze;
    if (jmevm_zip_entry_get(jar->zip, i, &ze) != 0)
      continue;
    /* Only .class files */
    if (ze.name_len < 6)
      continue;
    if (memcmp(ze.name + ze.name_len - 6, ".class", 6) != 0)
      continue;
    /* Skip module-info */
    if (ze.name_len >= 11 &&
        memcmp(ze.name + ze.name_len - 11, "module-info", 11) == 0)
      continue;

    size_t bytes_len = 0;
    uint8_t *bytes = jmevm_jar_read_entry(jar, i, &bytes_len);
    if (!bytes)
      continue;

    jmevm_classfile *cf = jmevm_classfile_load_from_buffer(bytes, bytes_len);
    /* Note: classfile keeps a pointer to bytes — we must NOT free it here.
     * Instead we accept this leak for now; a proper VM would own the buf. */
    if (!cf) {
      free(bytes);
      continue;
    }

    jmevm_vm_register_class(vm, cf);
    loaded++;
    if (verbose) {
      fprintf(stderr, "[load] %.*s\n", (int)ze.name_len, ze.name);
    }
    /* intentionally leaking `bytes` — cf->buf points into it */
    (void)bytes;
  }
  return loaded;
}

/* -------------------------------------------------------------------------
 * Load a single .class file from disk into the VM registry
 * ---------------------------------------------------------------------- */
static jmevm_classfile *load_class_file(jmevm_vm *vm, const char *path,
                                        int verbose) {
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
  jmevm_classfile *cf = jmevm_classfile_load_from_buffer(buf, (size_t)sz);
  if (!cf) {
    free(buf);
    return NULL;
  }
  jmevm_vm_register_class(vm, cf);
  if (verbose)
    fprintf(stderr, "[load] %s\n", path);
  return cf;
}

/* -------------------------------------------------------------------------
 * Find and run the main method of a given class name
 * ---------------------------------------------------------------------- */
static int run_main_class(jmevm_vm *vm, jmevm_classpath *cp,
                          const char *class_name, int verbose) {
  /* Try to find the class in the classpath first */
  size_t class_len = 0;
  uint8_t *class_buf = jmevm_classpath_find_class(cp, class_name, &class_len);

  jmevm_classfile *main_cf = NULL;

  if (class_buf) {
    main_cf = jmevm_classfile_load_from_buffer(class_buf, class_len);
    if (main_cf) {
      jmevm_vm_register_class(vm, main_cf);
      if (verbose)
        fprintf(stderr, "[load] %s (from classpath)\n", class_name);
    } else {
      free(class_buf);
    }
  }

  if (!main_cf) {
    /* Fall back: maybe it was already loaded (e.g. from a JAR) */
    /* try <class_name>.class in cwd */
    size_t nlen = strlen(class_name);
    char *path = (char *)malloc(nlen + 7);
    if (!path)
      return 1;
    snprintf(path, nlen + 7, "%s.class", class_name);

    FILE *probe = fopen(path, "rb");
    if (probe) {
      fclose(probe);
      main_cf = load_class_file(vm, path, verbose);
    }
    free(path);
  }

  if (!main_cf) {
    fprintf(stderr, "error: could not find or load class '%s'\n", class_name);
    return 1;
  }

  jmevm_main_method m;
  if (jmevm_classfile_extract_main(main_cf, &m) != 0) {
    /* Convert slashes to dots for the error message */
    char *display = strdup(class_name);
    if (display) {
      for (char *p = display; *p; p++)
        if (*p == '/')
          *p = '.';
    }
    fprintf(stderr, "error: Main method not found in class %s\n",
            display ? display : class_name);
    free(display);
    return 1;
  }

  jmevm_runtime_init_native();

  int rc =
      jmevm_vm_run(vm, main_cf, m.code, m.code_len, m.max_locals, m.max_stack);
  return (rc == 0) ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char **argv) {
  const char *prog = (argc > 0 && argv[0]) ? argv[0] : "jmevm";
  int verbose = 0;
  int jar_mode = 0; /* -jar flag */
  const char *jar_path = NULL;
  const char *main_class = NULL;

  jmevm_classpath *cp = jmevm_classpath_create();
  if (!cp) {
    fprintf(stderr, "Out of memory\n");
    return 1;
  }

  /* -----------------------------------------------------------------------
   * Argument parsing — identical semantics to the real `java` command
   * -------------------------------------------------------------------- */
  int i = 1;
  while (i < argc) {
    const char *arg = argv[i];

    if (strcmp(arg, "-help") == 0 || strcmp(arg, "--help") == 0 ||
        strcmp(arg, "-h") == 0) {
      usage(prog);
      jmevm_classpath_destroy(cp);
      return 0;
    }

    if (strcmp(arg, "-version") == 0 || strcmp(arg, "--version") == 0) {
      printf("jmevm 1.0  (minimal JVM for Java ME / CLDC)\n");
      jmevm_classpath_destroy(cp);
      return 0;
    }

    if (strcmp(arg, "-verbose") == 0 || strcmp(arg, "-verbose:class") == 0) {
      verbose = 1;
      i++;
      continue;
    }

    if ((strcmp(arg, "-cp") == 0 || strcmp(arg, "-classpath") == 0) &&
        i + 1 < argc) {
      jmevm_classpath_add_path(cp, argv[++i]);
      i++;
      continue;
    }

    /* -cp=<path> or -classpath=<path> */
    if ((strncmp(arg, "-cp=", 4) == 0)) {
      jmevm_classpath_add_path(cp, arg + 4);
      i++;
      continue;
    }
    if ((strncmp(arg, "-classpath=", 11) == 0)) {
      jmevm_classpath_add_path(cp, arg + 11);
      i++;
      continue;
    }

    if (strcmp(arg, "-jar") == 0 && i + 1 < argc) {
      jar_mode = 1;
      jar_path = argv[++i];
      i++;
      continue;
    }

    /* Ignore common real-jvm flags we don't support */
    if (strncmp(arg, "-X", 2) == 0 || strncmp(arg, "-D", 2) == 0 ||
        strncmp(arg, "-ea", 3) == 0 || strncmp(arg, "-da", 3) == 0 ||
        strcmp(arg, "-server") == 0 || strcmp(arg, "-client") == 0) {
      i++;
      continue;
    }

    /* First non-flag argument is the main class */
    if (arg[0] != '-') {
      main_class = arg;
      i++;
      break; /* rest are program args (ignored in this minimal VM) */
    }

    fprintf(stderr, "warning: unknown option '%s' (ignored)\n", arg);
    i++;
  }

  if (!jar_mode && !main_class) {
    usage(prog);
    jmevm_classpath_destroy(cp);
    return 1;
  }

  /* -----------------------------------------------------------------------
   * Boot the VM
   * -------------------------------------------------------------------- */
  jmevm_vm *vm = jmevm_vm_create(); /* also calls jmevm_cldc_init() */
  if (!vm) {
    fprintf(stderr, "error: failed to create VM\n");
    jmevm_classpath_destroy(cp);
    return 1;
  }

  /* -----------------------------------------------------------------------
   * -jar mode: load JAR, get Main-Class from manifest
   * -------------------------------------------------------------------- */
  if (jar_mode) {
    jmevm_jar *jar = jmevm_jar_open(jar_path);
    if (!jar) {
      fprintf(stderr, "error: could not open JAR: %s\n", jar_path);
      jmevm_vm_destroy(vm);
      jmevm_classpath_destroy(cp);
      return 1;
    }
    if (!jar->main_class) {
      fprintf(stderr, "error: no Main-Class attribute in %s manifest\n",
              jar_path);
      jmevm_jar_close(jar);
      jmevm_vm_destroy(vm);
      jmevm_classpath_destroy(cp);
      return 1;
    }
    main_class = strdup(jar->main_class); /* keep after jar_close */
    if (verbose)
      fprintf(stderr, "[jar] Main-Class: %s\n", main_class);

    load_all_classes_from_jar(vm, jar, verbose);
    jmevm_jar_close(jar);
  }

  /* -----------------------------------------------------------------------
   * Load all classes from every JAR/dir in the classpath eagerly.
   * (For a minimal VM this avoids lazy-loading complexity.)
   * -------------------------------------------------------------------- */
  for (size_t j = 0; j < cp->count; j++) {
    jmevm_cp_entry *e = &cp->entries[j];
    if (e->kind == JMEVM_CP_JAR && e->jar) {
      load_all_classes_from_jar(vm, e->jar, verbose);
    }
    /* JMEVM_CP_DIR and JMEVM_CP_CLASS are searched lazily in run_main_class */
  }

  /* -----------------------------------------------------------------------
   * If the argument looks like a file path ending in .class, treat it as a
   * convenience shorthand: add its directory to the classpath and derive
   * the class name from the file name.
   * e.g.  "samples/ExceptionTest.class"  ->  cp += "samples/", cls =
   * "ExceptionTest"
   * -------------------------------------------------------------------- */
  const char *class_for_name = main_class;
  char *class_path_buf = NULL; /* owns derived class name if set */

  if (!jar_mode) {
    size_t mlen = strlen(main_class);
    if (mlen > 6 && strcmp(main_class + mlen - 6, ".class") == 0) {
      /* Strip .class suffix */
      class_path_buf = strdup(main_class);
      if (!class_path_buf) {
        jmevm_vm_destroy(vm);
        jmevm_classpath_destroy(cp);
        return 1;
      }
      class_path_buf[mlen - 6] = '\0';
      /* Find directory separator */
      char *sep = strrchr(class_path_buf, '/');
      if (!sep)
        sep = strrchr(class_path_buf, '\\');
      if (sep) {
        *sep = '\0';
        /* Add directory to classpath */
        jmevm_classpath_add(cp, class_path_buf);
        class_for_name = sep + 1;
      } else {
        /* No directory: use "." */
        jmevm_classpath_add(cp, ".");
        class_for_name = class_path_buf;
      }
    }
  }

  /* -----------------------------------------------------------------------
   * Convert dotted class name to slash form: com.example.Main ->
   * com/example/Main
   * -------------------------------------------------------------------- */
  char *class_slashed = strdup(class_for_name);
  free(class_path_buf);
  if (!class_slashed) {
    jmevm_vm_destroy(vm);
    jmevm_classpath_destroy(cp);
    return 1;
  }
  for (char *p = class_slashed; *p; p++)
    if (*p == '.')
      *p = '/';

  /* -----------------------------------------------------------------------
   * Run
   * -------------------------------------------------------------------- */
  int rc = run_main_class(vm, cp, class_slashed, verbose);

  free(class_slashed);
  if (jar_mode)
    free((void *)main_class);
  jmevm_vm_destroy(vm);
  jmevm_classpath_destroy(cp);
  return rc;
}
