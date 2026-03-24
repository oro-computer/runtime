#ifndef ORO_RUNTIME_ASN1_CONFIG_H
#define ORO_RUNTIME_ASN1_CONFIG_H

/*
 * Minimal configuration header for the bundled asn1c libraries.
 * The upstream project normally generates this file via autotools.
 * For the runtime integration we provide a hand crafted subset
 * that covers the supported platforms (desktop, mobile, windows).
 */

#if !defined(HAVE_CONFIG_H)
#define HAVE_CONFIG_H 1
#endif

/*
 * Basic package metadata consumed by the asn1c sources for banner strings
 * and diagnostics. Keep these aligned with the bundled asn1c release.
 */
#if !defined(PACKAGE_NAME)
#define PACKAGE_NAME "asn1c"
#endif
#if !defined(PACKAGE_TARNAME)
#define PACKAGE_TARNAME "asn1c"
#endif
#if !defined(PACKAGE_VERSION)
#define PACKAGE_VERSION "0.9.28"
#endif
#if !defined(PACKAGE_STRING)
#define PACKAGE_STRING PACKAGE_NAME " " PACKAGE_VERSION
#endif
#if !defined(PACKAGE_BUGREPORT)
#define PACKAGE_BUGREPORT "vlm@lionet.info"
#endif
#if !defined(PACKAGE_URL)
#define PACKAGE_URL "http://lionet.info/asn1c"
#endif
#if !defined(VERSION)
#define VERSION PACKAGE_VERSION
#endif

/*
 * asn1c uses GNU typeof() and statement expressions in a couple of queue
 * helpers. Clang in strict C17 mode drops the GNU keyword, so provide a
 * portable alias when the compiler supports the extension.
 */
#if (defined(__clang__) || defined(__GNUC__)) && !defined(typeof)
#define typeof __typeof__
#endif

/*
 * Enable POSIX/GNU prototypes (e.g. strdup) on libcs that gate them
 * behind feature-test macros.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

/* Standard headers available across our supported toolchains. */
#define HAVE_SYS_TYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1

#if defined(_WIN32)
  #define HAVE_SYS_STAT_H 1
  #define HAVE_SYS_PARAM_H 0
  #define HAVE_UNISTD_H 0
  #define HAVE_SYMLINK 0
  #define HAVE_MKSTEMPS 0
  #define HAVE_DECL_STRCASECMP 0
#else
  #define HAVE_SYS_STAT_H 1
  #define HAVE_SYS_PARAM_H 1
  #define HAVE_UNISTD_H 1
  #define HAVE_SYMLINK 1
  #define HAVE_MKSTEMPS 1
  #define HAVE_DECL_STRCASECMP 1
#endif

/*
 * mergesort is provided by BSD libcs (including macOS) but not by glibc.
 * Do not define HAVE_MERGESORT when the libc does not expose it so the
 * upstream sources take the qsort fallback.  The asn1c code uses #ifdef
 * checks, so a false-positive definition causes implicit declarations on
 * Linux and other platforms.
 */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
  #define HAVE_MERGESORT 1
#else
  #undef HAVE_MERGESORT
#endif

/* Wide integer parsing helpers (guarded in the sources). */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
  #define HAVE_STRTOIMAX 1
#else
  #define HAVE_STRTOIMAX 0
#endif
#define HAVE_STRTOLL 1

#endif /* ORO_RUNTIME_ASN1_CONFIG_H */
