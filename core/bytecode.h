#ifndef JVM_CORE_BYTECODE_H
#define JVM_CORE_BYTECODE_H

#define JVM_OP_ICONST_M1 0x02
#define JVM_OP_ICONST_0 0x03
#define JVM_OP_ICONST_1 0x04
#define JVM_OP_ICONST_2 0x05
#define JVM_OP_ICONST_3 0x06
#define JVM_OP_ICONST_4 0x07
#define JVM_OP_ICONST_5 0x08

#define JVM_OP_ILOAD 0x15
#define JVM_OP_ILOAD_0 0x1a
#define JVM_OP_ILOAD_1 0x1b
#define JVM_OP_ILOAD_2 0x1c
#define JVM_OP_ILOAD_3 0x1d

#define JVM_OP_ISTORE 0x36
#define JVM_OP_ISTORE_0 0x3b
#define JVM_OP_ISTORE_1 0x3c
#define JVM_OP_ISTORE_2 0x3d
#define JVM_OP_ISTORE_3 0x3e

#define JVM_OP_IADD 0x60
#define JVM_OP_ISUB 0x64

#define JVM_OP_INVOKESTATIC 0xb8

#define JVM_OP_GETSTATIC 0xb2
#define JVM_OP_INVOKEVIRTUAL 0xb6
#define JVM_OP_INVOKESPECIAL 0xb7
#define JVM_OP_DUP 0x59

#define JVM_OP_IRETURN 0xac

#define JVM_OP_RETURN 0xb1
#define JVM_OP_NEW 0xbb
#define JVM_OP_GETFIELD 0xb4
#define JVM_OP_PUTFIELD 0xb5

#define JVM_OP_BIPUSH 0x10
#define JVM_OP_SIPUSH 0x11
#define JVM_OP_LDC 0x12

#define JVM_OP_ALOAD 0x19
#define JVM_OP_ALOAD_0 0x2a
#define JVM_OP_ALOAD_1 0x2b
#define JVM_OP_ALOAD_2 0x2c
#define JVM_OP_ALOAD_3 0x2d

#define JVM_OP_ASTORE 0x3a
#define JVM_OP_ASTORE_0 0x4b
#define JVM_OP_ASTORE_1 0x4c
#define JVM_OP_ASTORE_2 0x4d
#define JVM_OP_ASTORE_3 0x4e

#define JVM_OP_IALOAD 0x2e
#define JVM_OP_BALOAD 0x33

#define JVM_OP_IASTORE 0x4f
#define JVM_OP_BASTORE 0x54

#define JVM_OP_ARRAYLENGTH 0xbe

#define JVM_OP_NEWARRAY 0xbc

#define JVM_OP_ATHROW 0xbf

#define JVM_OP_IINC 0x84
#define JVM_OP_IF_ICMPGE 0xa2
#define JVM_OP_GOTO 0xa7

#endif
