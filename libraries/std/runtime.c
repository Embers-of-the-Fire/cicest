/// @file runtime.c
/// @brief Cicest language runtime — implementations of std prelude functions.
///
/// These functions are declared as `extern "lang" fn` in `prelude.cst` and
/// linked into every cicest executable. The signatures here must match the
/// LLVM IR declarations emitted by the codegen:
///
///   cstc_std_print(ptr)          → void
///   cstc_std_println(ptr)        → void
///   cstc_std_to_str(ptr,double)  → void
///   cstc_std_str_concat(ptr,ptr,ptr) → void
///   cstc_std_str_len(ptr)        → double
///   cstc_std_str_free(ptr)       → void
///   cstc_std_assert(i1)          → void
///   cstc_std_assert_eq(double,double) → void

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cstc_rt_str {
    char* data;
    uint64_t len;
    uint8_t owns_bytes;
} cstc_rt_str;

static char cstc_rt_empty_data[] = "";

static cstc_rt_str cstc_rt_borrowed_empty(void) {
    return (cstc_rt_str){
        .data = cstc_rt_empty_data,
        .len = 0,
        .owns_bytes = 0,
    };
}

static void cstc_rt_store_borrowed_empty(cstc_rt_str* out) {
    if (out != NULL)
        *out = cstc_rt_borrowed_empty();
}

void cstc_std_print(const cstc_rt_str* value) {
    if (value == NULL || value->data == NULL || value->len == 0)
        return;
    fwrite(value->data, 1, (size_t)value->len, stdout);
}

void cstc_std_println(const cstc_rt_str* value) {
    cstc_std_print(value);
    fputc('\n', stdout);
}

void cstc_std_to_str(cstc_rt_str* out, double value) {
    // snprintf with NULL to measure, then allocate + format.
    int len = snprintf(NULL, 0, "%g", value);
    if (len < 0)
        len = 0;

    char* buf = (char*)malloc((size_t)len + 1);
    if (buf == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    snprintf(buf, (size_t)len + 1, "%g", value);
    if (out == NULL) {
        free(buf);
        return;
    }

    out->data = buf;
    out->len = (uint64_t)len;
    out->owns_bytes = 1;
}

void cstc_std_str_concat(cstc_rt_str* out, const cstc_rt_str* a, const cstc_rt_str* b) {
    const size_t la = a != NULL ? (size_t)a->len : 0;
    const size_t lb = b != NULL ? (size_t)b->len : 0;
    char* buf = (char*)malloc(la + lb + 1);
    if (buf == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    if (la > 0 && a != NULL && a->data != NULL)
        memcpy(buf, a->data, la);
    if (lb > 0 && b != NULL && b->data != NULL)
        memcpy(buf + la, b->data, lb);
    buf[la + lb] = '\0';
    if (out == NULL) {
        free(buf);
        return;
    }

    out->data = buf;
    out->len = (uint64_t)(la + lb);
    out->owns_bytes = 1;
}

double cstc_std_str_len(const cstc_rt_str* value) {
    if (value == NULL)
        return 0.0;
    return (double)value->len;
}

void cstc_std_str_free(const cstc_rt_str* value) {
    if (value != NULL && value->owns_bytes != 0 && value->data != NULL)
        free((void*)value->data);
}

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
