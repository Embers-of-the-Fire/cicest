/// @file runtime.c
/// @brief Cicest language runtime — implementations of std prelude functions.
///
/// These functions are declared as `extern "lang" fn` in `prelude.cst` and
/// linked into every cicest executable. The signatures here must match the
/// LLVM IR declarations emitted by the codegen:
///
///   cstc_std_print(ptr)          → void
///   cstc_std_println(ptr)        → void
///   cstc_std_read_file(ptr,ptr)  → void
///   cstc_std_read_line(ptr)      → void
///   cstc_std_rand()              → double
///   cstc_std_time()              → double
///   cstc_std_env(ptr,ptr)        → void
///   cstc_std_to_str(ptr,double)  → void
///   cstc_std_str_concat(ptr,ptr,ptr) → void
///   cstc_std_str_len(ptr)        → double
///   cstc_std_str_free(ptr)       → void
///   cstc_std_assert(i1)          → void
///   cstc_std_assert_eq(double,double) → void
///   cstc_std_constraint(i1)      → { i32 }

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

typedef struct cstc_rt_str {
    char* data;
    uint64_t len;
    uint8_t owns_bytes;
} cstc_rt_str;

typedef struct cstc_rt_constraint {
    int32_t discriminant;
} cstc_rt_constraint;

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

static bool cstc_rt_str_is_missing_or_empty(const cstc_rt_str* value) {
    return value == NULL || value->data == NULL || value->len == 0;
}

static void cstc_rt_store_owned(cstc_rt_str* out, char* data, size_t len) {
    if (out == NULL) {
        free(data);
        return;
    }

    if (data == NULL || len == 0) {
        free(data);
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    out->data = data;
    out->len = (uint64_t)len;
    out->owns_bytes = 1;
}

static int cstc_rt_reserve(char** data, size_t* capacity, size_t required) {
    if (data == NULL || capacity == NULL)
        return 0;
    if (required <= *capacity)
        return 1;

    size_t next = *capacity == 0 ? 64 : *capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            next = required;
            break;
        }
        next *= 2;
    }

    char* grown = (char*)realloc(*data, next);
    if (grown == NULL)
        return 0;

    *data = grown;
    *capacity = next;
    return 1;
}

static void cstc_rt_finalize_buffer(cstc_rt_str* out, char* data, size_t len) {
    if (data == NULL || len == 0) {
        free(data);
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    char* resized = (char*)realloc(data, len + 1);
    if (resized != NULL)
        data = resized;
    data[len] = '\0';
    cstc_rt_store_owned(out, data, len);
}

static int cstc_rt_get_timespec_utc(struct timespec* ts) {
    if (ts == NULL)
        return 0;

#ifdef TIME_UTC
    if (timespec_get(ts, TIME_UTC) == TIME_UTC)
        return 1;
#endif

#ifdef _WIN32
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    const uint64_t windows_epoch_offset = 116444736000000000ULL;

    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    if (ticks.QuadPart < windows_epoch_offset)
        return 0;

    const uint64_t unix_ticks = ticks.QuadPart - windows_epoch_offset;
    ts->tv_sec = (time_t)(unix_ticks / 10000000ULL);
    ts->tv_nsec = (long)((unix_ticks % 10000000ULL) * 100ULL);
    return 1;
#else
    return 0;
#endif
}

static char* cstc_rt_to_c_string(const cstc_rt_str* value) {
    if (cstc_rt_str_is_missing_or_empty(value))
        return NULL;
    if (value->len > (uint64_t)SIZE_MAX - 1)
        return NULL;

    const size_t len = (size_t)value->len;
    char* buffer = (char*)malloc(len + 1);
    if (buffer == NULL)
        return NULL;

    memcpy(buffer, value->data, len);
    buffer[len] = '\0';
    return buffer;
}

void cstc_std_print(const cstc_rt_str* value) {
    if (cstc_rt_str_is_missing_or_empty(value))
        return;
    fwrite(value->data, 1, (size_t)value->len, stdout);
}

void cstc_std_println(const cstc_rt_str* value) {
    if (cstc_rt_str_is_missing_or_empty(value))
        return;
    cstc_std_print(value);
    fputc('\n', stdout);
}

void cstc_std_read_file(cstc_rt_str* out, const cstc_rt_str* path) {
    if (out == NULL)
        return;

    char* c_path = cstc_rt_to_c_string(path);
    if (c_path == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    FILE* file = fopen(c_path, "rb");
    free(c_path);
    if (file == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    char* data = NULL;
    size_t len = 0;
    size_t capacity = 0;
    unsigned char chunk[4096];

    while (1) {
        const size_t read = fread(chunk, 1, sizeof(chunk), file);
        if (read > 0) {
            if (!cstc_rt_reserve(&data, &capacity, len + read + 1)) {
                free(data);
                fclose(file);
                cstc_rt_store_borrowed_empty(out);
                return;
            }
            memcpy(data + len, chunk, read);
            len += read;
        }

        if (read < sizeof(chunk)) {
            if (ferror(file)) {
                free(data);
                fclose(file);
                cstc_rt_store_borrowed_empty(out);
                return;
            }
            break;
        }
    }

    fclose(file);
    cstc_rt_finalize_buffer(out, data, len);
}

void cstc_std_read_line(cstc_rt_str* out) {
    if (out == NULL)
        return;

    char* data = NULL;
    size_t len = 0;
    size_t capacity = 0;

    while (1) {
        const int ch = fgetc(stdin);
        if (ch == EOF)
            break;
        if (ch == '\n')
            break;

        if (!cstc_rt_reserve(&data, &capacity, len + 2)) {
            free(data);
            cstc_rt_store_borrowed_empty(out);
            return;
        }
        data[len++] = (char)ch;
    }

    if (len > 0 && data[len - 1] == '\r')
        --len;

    cstc_rt_finalize_buffer(out, data, len);
}

static void cstc_rt_seed_rand_once(void) {
    static int seeded = 0;

    if (seeded)
        return;

    struct timespec ts;
    unsigned int seed = (unsigned int)time(NULL);
    if (cstc_rt_get_timespec_utc(&ts))
        seed ^= (unsigned int)ts.tv_sec ^ (unsigned int)ts.tv_nsec;

    srand(seed);
    seeded = 1;
}

double cstc_std_rand(void) {
    cstc_rt_seed_rand_once();
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

double cstc_std_time(void) {
    struct timespec ts;
    if (cstc_rt_get_timespec_utc(&ts))
        return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
    return (double)time(NULL);
}

void cstc_std_env(cstc_rt_str* out, const cstc_rt_str* name) {
    if (out == NULL)
        return;

    char* c_name = cstc_rt_to_c_string(name);
    if (c_name == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    const char* value = getenv(c_name);
    free(c_name);
    if (value == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    const size_t len = strlen(value);
    char* copy = (char*)malloc(len + 1);
    if (copy == NULL) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    memcpy(copy, value, len + 1);
    cstc_rt_store_owned(out, copy, len);
}

void cstc_std_to_str(cstc_rt_str* out, double value) {
    if (out == NULL)
        return;

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

    out->data = buf;
    out->len = (uint64_t)len;
    out->owns_bytes = 1;
}

void cstc_std_str_concat(cstc_rt_str* out, const cstc_rt_str* a, const cstc_rt_str* b) {
    if (out == NULL)
        return;

    const uint64_t raw_la = a != NULL ? a->len : 0;
    const uint64_t raw_lb = b != NULL ? b->len : 0;

    if (raw_la > (uint64_t)SIZE_MAX || raw_lb > (uint64_t)SIZE_MAX) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

    const size_t la = (size_t)raw_la;
    const size_t lb = (size_t)raw_lb;
    if (la >= SIZE_MAX - lb) {
        cstc_rt_store_borrowed_empty(out);
        return;
    }

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

cstc_rt_constraint cstc_std_constraint(int value) {
    return (cstc_rt_constraint){
        .discriminant = value ? 0 : 1,
    };
}
