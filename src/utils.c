#define _POSIX_C_SOURCE 200809L

#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRESS_BAR_WIDTH 30U

void print_usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [-n N] [-s SEED | -S seed1,seed2,...] [-o OUTPUT] "
        "[-m MODE] [-L BLOCK_L] [-p]\n"
        "  -n, --n       Number of insertions (default: 10000)\n"
        "  -s, --seed    Single random seed (default: 123456789)\n"
        "  -S, --seeds   Comma-separated seed list, e.g. 1,2,3\n"
        "  -o, --output  CSV output path (default: insert_bench.csv)\n"
        "  -m, --mode    Benchmark mode: array | list | block | both | all "
        "(default: both)\n"
        "  -L, --block-l Block base length L for block list (range L..2L, "
        "default: 64)\n"
        "  -p, --progress  Show progress bar on stderr (optional)\n"
        "  -h, --help    Show this help message\n",
        prog
    );
}

bool parse_size_t_arg(const char *text, size_t *out) {
    char *end = NULL;
    unsigned long long value = 0;

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > SIZE_MAX) {
        return false;
    }

    *out = (size_t)value;
    return true;
}

bool parse_seed_arg(const char *text, unsigned int *out) {
    char *end = NULL;
    unsigned long value = 0;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX) {
        return false;
    }

    *out = (unsigned int)value;
    return true;
}

static char *trim_inplace(char *s) {
    char *end = NULL;
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    return s;
}

bool parse_seed_list_arg(const char *text, unsigned int **out, size_t *out_count) {
    char *copy = NULL;
    char *saveptr = NULL;
    char *token = NULL;
    unsigned int *items = NULL;
    size_t count = 0;
    size_t capacity = 4;

    if (text == NULL || *text == '\0' || out == NULL || out_count == NULL) {
        return false;
    }

    copy = strdup(text);
    if (copy == NULL) {
        return false;
    }

    items = checked_malloc_array(capacity, sizeof(items[0]), "seed list");
    if (items == NULL) {
        free(copy);
        return false;
    }

    token = strtok_r(copy, ",", &saveptr);
    while (token != NULL) {
        char *clean = trim_inplace(token);
        unsigned int seed_value = 0;
        unsigned int *resized = NULL;

        if (*clean == '\0' || !parse_seed_arg(clean, &seed_value)) {
            free(items);
            free(copy);
            return false;
        }

        if (count == capacity) {
            if (capacity > SIZE_MAX / 2) {
                free(items);
                free(copy);
                return false;
            }
            capacity *= 2;
            resized = realloc(items, capacity * sizeof(items[0]));
            if (resized == NULL) {
                free(items);
                free(copy);
                return false;
            }
            items = resized;
        }

        items[count++] = seed_value;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(copy);

    if (count == 0) {
        free(items);
        return false;
    }

    *out = items;
    *out_count = count;
    return true;
}

bool parse_mode_arg(const char *text, BenchMode *out) {
    if (strcmp(text, "both") == 0) {
        *out = MODE_BOTH;
        return true;
    }
    if (strcmp(text, "array") == 0) {
        *out = MODE_ARRAY;
        return true;
    }
    if (strcmp(text, "list") == 0) {
        *out = MODE_LIST;
        return true;
    }
    if (strcmp(text, "block") == 0) {
        *out = MODE_BLOCK;
        return true;
    }
    if (strcmp(text, "all") == 0) {
        *out = MODE_ALL;
        return true;
    }
    return false;
}

bool mode_has_array(BenchMode mode) {
    return mode == MODE_BOTH || mode == MODE_ARRAY || mode == MODE_ALL;
}

bool mode_has_list(BenchMode mode) {
    return mode == MODE_BOTH || mode == MODE_LIST || mode == MODE_ALL;
}

bool mode_has_block(BenchMode mode) {
    return mode == MODE_BLOCK || mode == MODE_ALL;
}

const char *mode_to_string(BenchMode mode) {
    if (mode == MODE_ARRAY) {
        return "array";
    }
    if (mode == MODE_LIST) {
        return "list";
    }
    if (mode == MODE_BLOCK) {
        return "block";
    }
    if (mode == MODE_ALL) {
        return "all";
    }
    return "both";
}

void *checked_malloc_array(size_t count, size_t elem_size, const char *name) {
    void *ptr = NULL;

    if (count > 0 && elem_size > SIZE_MAX / count) {
        fprintf(stderr, "Allocation overflow for %s\n", name);
        return NULL;
    }

    ptr = malloc(count * elem_size);
    if (ptr == NULL) {
        fprintf(stderr, "Allocation failed for %s\n", name);
    }

    return ptr;
}

uint64_t diff_ns(const struct timespec *start, const struct timespec *end) {
    uint64_t s = (uint64_t)start->tv_sec * 1000000000ULL + (uint64_t)start->tv_nsec;
    uint64_t e = (uint64_t)end->tv_sec * 1000000000ULL + (uint64_t)end->tv_nsec;
    return e - s;
}

uint64_t timespec_to_ns(const struct timespec *t) {
    return (uint64_t)t->tv_sec * 1000000000ULL + (uint64_t)t->tv_nsec;
}

static double seconds_between(const struct timespec *start, const struct timespec *end) {
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

size_t progress_stride(size_t total) {
    size_t stride = total / 200;
    if (stride == 0) {
        return 1;
    }
    return stride;
}

void print_progress(
    const char *phase,
    size_t done,
    size_t total,
    const struct timespec *phase_start,
    const struct timespec *now
) {
    double pct = 100.0;
    double elapsed = 0.0;
    double eta = 0.0;
    size_t filled = PROGRESS_BAR_WIDTH;
    size_t j = 0;

    if (total > 0) {
        pct = (double)done * 100.0 / (double)total;
    }
    if (pct < 0.0) {
        pct = 0.0;
    }
    if (pct > 100.0) {
        pct = 100.0;
    }

    elapsed = seconds_between(phase_start, now);
    if (done > 0 && done < total) {
        eta = elapsed * ((double)(total - done) / (double)done);
    }

    filled = (size_t)((pct / 100.0) * (double)PROGRESS_BAR_WIDTH);
    if (filled > PROGRESS_BAR_WIDTH) {
        filled = PROGRESS_BAR_WIDTH;
    }

    fprintf(stderr, "\r[%s] [", phase);
    for (j = 0; j < PROGRESS_BAR_WIDTH; ++j) {
        fputc((j < filled) ? '#' : '-', stderr);
    }
    fprintf(
        stderr,
        "] %6.2f%% (%zu/%zu) elapsed %.1fs eta %.1fs",
        pct,
        done,
        total,
        elapsed,
        eta
    );
    if (done >= total) {
        fputc('\n', stderr);
    }
    fflush(stderr);
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t lhs = *(const uint64_t *)a;
    uint64_t rhs = *(const uint64_t *)b;
    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    return 0;
}

static size_t percentile_index(size_t n, unsigned int pct) {
    size_t rank = 0;
    if (n == 0) {
        return 0;
    }

    rank = (n * (size_t)pct + 99U) / 100U;
    if (rank == 0) {
        rank = 1;
    }
    return rank - 1;
}

bool build_summary(const uint64_t *times, size_t n, SummaryStats *out) {
    size_t i = 0;
    uint64_t *sorted = NULL;

    if (n == 0 || times == NULL || out == NULL) {
        return false;
    }

    out->total_ns = 0;
    out->min_ns = times[0];
    out->max_ns = times[0];

    for (i = 0; i < n; ++i) {
        out->total_ns += times[i];
        if (times[i] < out->min_ns) {
            out->min_ns = times[i];
        }
        if (times[i] > out->max_ns) {
            out->max_ns = times[i];
        }
    }
    out->avg_ns = (double)out->total_ns / (double)n;

    sorted = checked_malloc_array(n, sizeof(sorted[0]), "sorted times");
    if (sorted == NULL) {
        return false;
    }
    memcpy(sorted, times, n * sizeof(sorted[0]));
    qsort(sorted, n, sizeof(sorted[0]), cmp_u64);

    out->p50_ns = sorted[percentile_index(n, 50)];
    out->p95_ns = sorted[percentile_index(n, 95)];
    out->p99_ns = sorted[percentile_index(n, 99)];

    free(sorted);
    return true;
}

void print_summary_line(const char *name, const SummaryStats *s) {
    printf(
        "%s: total=%" PRIu64 " ns (%.3f ms), avg=%.2f ns, min=%" PRIu64
        ", p50=%" PRIu64 ", p95=%" PRIu64 ", p99=%" PRIu64 ", max=%" PRIu64 "\n",
        name,
        s->total_ns,
        (double)s->total_ns / 1000000.0,
        s->avg_ns,
        s->min_ns,
        s->p50_ns,
        s->p95_ns,
        s->p99_ns,
        s->max_ns
    );
}
