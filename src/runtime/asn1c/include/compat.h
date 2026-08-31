#ifndef ORO_RUNTIME_ASN1C_COMPAT_H
#define ORO_RUNTIME_ASN1C_COMPAT_H

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <alloca.h>
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* ORO_RUNTIME_ASN1C_COMPAT_H */
