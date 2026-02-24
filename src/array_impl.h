#ifndef ARRAY_IMPL_H
#define ARRAY_IMPL_H

#include "bench_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void array_insert(ArrayState *array, size_t pos, int64_t value);
bool verify_array_vs_list(const ArrayState *array, const ListState *list);
bool verify_array_vs_block(const ArrayState *array, const BlockListState *block);

#endif
