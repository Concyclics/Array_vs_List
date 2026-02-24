#ifndef UTILS_H
#define UTILS_H

#include "bench_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

void print_usage(const char *prog);

bool parse_size_t_arg(const char *text, size_t *out);
bool parse_seed_arg(const char *text, unsigned int *out);
bool parse_seed_list_arg(const char *text, unsigned int **out, size_t *out_count);
bool parse_mode_arg(const char *text, BenchMode *out);

bool mode_has_array(BenchMode mode);
bool mode_has_list(BenchMode mode);
bool mode_has_block(BenchMode mode);
const char *mode_to_string(BenchMode mode);

void *checked_malloc_array(size_t count, size_t elem_size, const char *name);

uint64_t diff_ns(const struct timespec *start, const struct timespec *end);
uint64_t timespec_to_ns(const struct timespec *t);
size_t progress_stride(size_t total);
void print_progress(
    const char *phase,
    size_t done,
    size_t total,
    const struct timespec *phase_start,
    const struct timespec *now
);

bool build_summary(const uint64_t *times, size_t n, SummaryStats *out);
void print_summary_line(const char *name, const SummaryStats *s);

#endif
