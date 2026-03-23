/// @file runtime.c
/// @brief Cicest language runtime — implementations of std prelude functions.
///
/// These functions are declared as `extern "lang" fn` in `prelude.cst` and
/// linked into every cicest executable. The signatures here must match the
/// LLVM IR declarations emitted by the codegen:
///
///   cstc_std_print(ptr)          → void
///   cstc_std_println(ptr)        → void
///   cstc_std_to_str(double)      → ptr
///   cstc_std_str_concat(ptr,ptr) → ptr
///   cstc_std_str_len(ptr)        → double
///   cstc_std_str_free(ptr)       → void
///   cstc_std_assert(i1)          → void
///   cstc_std_assert_eq(double,double) → void

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cstc_std_print(const char* value) { fputs(value, stdout); }

void cstc_std_println(const char* value) { puts(value); }

char* cstc_std_to_str(double value) {
    // snprintf with NULL to measure, then allocate + format.
    int len = snprintf(NULL, 0, "%g", value);
    if (len < 0)
        len = 0;
    char* buf = (char*)malloc((size_t)len + 1);
    if (buf == NULL) {
        // Allocation failure: return a heap-allocated empty string so that
        // callers always receive a freeable pointer.
        buf = (char*)malloc(1);
        if (buf != NULL)
            buf[0] = '\0';
        return buf;
    }
    snprintf(buf, (size_t)len + 1, "%g", value);
    return buf;
}

char* cstc_std_str_concat(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* buf = (char*)malloc(la + lb + 1);
    if (buf == NULL) {
        // Allocation failure: return a heap-allocated empty string.
        buf = (char*)malloc(1);
        if (buf != NULL)
            buf[0] = '\0';
        return buf;
    }
    memcpy(buf, a, la);
    memcpy(buf + la, b, lb);
    buf[la + lb] = '\0';
    return buf;
}

double cstc_std_str_len(const char* value) { return (double)strlen(value); }

void cstc_std_str_free(const char* value) { free((void*)value); }

void cstc_std_assert(int condition) {
    if (!condition) {
        fputs("assertion failed\n", stderr);
        exit(1);
    }
}

void cstc_std_assert_eq(double a, double b) {
    const double epsilon = 1e-9;
    if (fabs(a - b) > epsilon) {
        fprintf(stderr, "assertion failed: %g != %g\n", a, b);
        exit(1);
    }
}
