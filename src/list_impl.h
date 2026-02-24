#ifndef LIST_IMPL_H
#define LIST_IMPL_H

#include "bench_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void list_insert(ListState *list, size_t pos, int64_t value);
bool verify_list_vs_block(const ListState *list, const BlockListState *block);

#endif
