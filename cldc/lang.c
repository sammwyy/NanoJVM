#include "cldc/lang.h"
#include "cldc/generated/ArrayIndexOutOfBoundsException.h"
#include "cldc/generated/Exception.h"
#include "cldc/generated/NullPointerException.h"
#include "cldc/generated/Object.h"
#include "cldc/generated/RuntimeException.h"
#include "cldc/generated/String.h"
#include "cldc/generated/System.h"
#include "cldc/generated/Throwable.h"

void jvm_cldc_init(jvm_vm *vm) {
  jvm_classfile *cf;

  /* Load in dependency order: Object → Throwable → Exception → ... */
  cf = jvm_classfile_load_from_buffer(java_lang_Object_class,
                                      java_lang_Object_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_Throwable_class,
                                      java_lang_Throwable_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_Exception_class,
                                      java_lang_Exception_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_RuntimeException_class,
                                      java_lang_RuntimeException_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_NullPointerException_class,
                                      java_lang_NullPointerException_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(
      java_lang_ArrayIndexOutOfBoundsException_class,
      java_lang_ArrayIndexOutOfBoundsException_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_System_class,
                                      java_lang_System_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);

  cf = jvm_classfile_load_from_buffer(java_lang_String_class,
                                      java_lang_String_class_len);
  if (cf)
    jvm_vm_register_class(vm, cf);
}
