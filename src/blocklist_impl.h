#ifndef BLOCKLIST_IMPL_H
#define BLOCKLIST_IMPL_H

#include "bench_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool block_list_init(BlockListState *state, size_t n, size_t base_len);
void block_list_reset(BlockListState *state);
void block_list_free(BlockListState *state);
bool block_list_insert(BlockListState *state, size_t pos, int64_t value);

#endif
