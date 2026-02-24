#ifndef BENCH_TYPES_H
#define BENCH_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int64_t *data;
    size_t size;
    size_t capacity;
} ArrayState;

typedef struct Node {
    int64_t value;
    struct Node *next;
} Node;

typedef struct {
    Node *head;
    size_t size;
    Node *pool;
    size_t pool_used;
    size_t pool_capacity;
} ListState;

typedef struct {
    uint64_t total_ns;
    double avg_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t p50_ns;
    uint64_t p95_ns;
    uint64_t p99_ns;
} SummaryStats;

typedef enum {
    MODE_BOTH = 0,
    MODE_ARRAY = 1,
    MODE_LIST = 2,
    MODE_BLOCK = 3,
    MODE_ALL = 4
} BenchMode;

typedef struct {
    int64_t *values;
    size_t len;
} BlockNode;

typedef struct {
    BlockNode *blocks;
    size_t *prefix;
    int64_t *data_pool;
    size_t block_count;
    size_t max_blocks;
    size_t pool_used;
    size_t total_size;
    size_t base_len;
    size_t block_capacity;
} BlockListState;

#endif
