#include "blocklist_impl.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void block_rebuild_prefix_from(BlockListState *state, size_t start_idx) {
    size_t i = 0;
    size_t sum = 0;

    if (state == NULL || state->block_count == 0) {
        return;
    }

    if (start_idx > 0) {
        sum = state->prefix[start_idx - 1];
    }
    for (i = start_idx; i < state->block_count; ++i) {
        sum += state->blocks[i].len;
        state->prefix[i] = sum;
    }
}

static bool block_locate_insert(
    const BlockListState *state,
    size_t pos,
    size_t *block_idx,
    size_t *offset
) {
    size_t left = 0;
    size_t right = 0;
    size_t mid = 0;
    size_t idx = 0;
    size_t prev_prefix = 0;

    if (state == NULL || block_idx == NULL || offset == NULL || state->block_count == 0) {
        return false;
    }
    if (pos > state->total_size) {
        return false;
    }

    if (pos == state->total_size) {
        idx = state->block_count - 1;
        *block_idx = idx;
        *offset = state->blocks[idx].len;
        return true;
    }

    left = 0;
    right = state->block_count - 1;
    while (left < right) {
        mid = left + (right - left) / 2;
        if (state->prefix[mid] > pos) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    idx = left;
    prev_prefix = (idx == 0) ? 0 : state->prefix[idx - 1];
    *block_idx = idx;
    *offset = pos - prev_prefix;
    return true;
}

bool block_list_init(BlockListState *state, size_t n, size_t base_len) {
    size_t max_blocks = 0;
    size_t block_capacity = 0;
    size_t data_elems = 0;

    if (state == NULL || base_len == 0) {
        return false;
    }

    max_blocks = n / base_len + 2;
    if (max_blocks < 1) {
        max_blocks = 1;
    }
    if (base_len > (SIZE_MAX - 1) / 2) {
        return false;
    }
    block_capacity = base_len * 2 + 1;
    if (max_blocks > SIZE_MAX / block_capacity) {
        return false;
    }
    data_elems = max_blocks * block_capacity;

    state->blocks = checked_malloc_array(max_blocks, sizeof(state->blocks[0]), "block nodes");
    state->prefix = checked_malloc_array(max_blocks, sizeof(state->prefix[0]), "block prefix");
    state->data_pool = checked_malloc_array(data_elems, sizeof(state->data_pool[0]), "block data pool");
    if (state->blocks == NULL || state->prefix == NULL || state->data_pool == NULL) {
        return false;
    }

    state->max_blocks = max_blocks;
    state->base_len = base_len;
    state->block_capacity = block_capacity;
    state->pool_used = 1;
    state->block_count = 1;
    state->total_size = 0;
    state->blocks[0].values = state->data_pool;
    state->blocks[0].len = 0;
    state->prefix[0] = 0;
    return true;
}

void block_list_reset(BlockListState *state) {
    if (state == NULL || state->blocks == NULL || state->data_pool == NULL) {
        return;
    }
    state->pool_used = 1;
    state->block_count = 1;
    state->total_size = 0;
    state->blocks[0].values = state->data_pool;
    state->blocks[0].len = 0;
    state->prefix[0] = 0;
}

void block_list_free(BlockListState *state) {
    if (state == NULL) {
        return;
    }
    free(state->blocks);
    free(state->prefix);
    free(state->data_pool);
    state->blocks = NULL;
    state->prefix = NULL;
    state->data_pool = NULL;
}

bool block_list_insert(BlockListState *state, size_t pos, int64_t value) {
    size_t idx = 0;
    size_t offset = 0;
    BlockNode *blk = NULL;
    size_t move_count = 0;
    size_t i = 0;

    if (state == NULL || state->blocks == NULL || state->prefix == NULL) {
        return false;
    }
    if (!block_locate_insert(state, pos, &idx, &offset)) {
        return false;
    }

    blk = &state->blocks[idx];
    if (blk->len >= state->block_capacity) {
        return false;
    }

    move_count = blk->len - offset;
    if (move_count > 0) {
        memmove(
            &blk->values[offset + 1],
            &blk->values[offset],
            move_count * sizeof(blk->values[0])
        );
    }
    blk->values[offset] = value;
    blk->len += 1;
    state->total_size += 1;

    if (blk->len > state->base_len * 2) {
        size_t old_len = blk->len;
        size_t left_len = old_len / 2;
        size_t right_len = old_len - left_len;
        int64_t *right_src = &blk->values[left_len];
        BlockNode new_blk;

        if (state->block_count >= state->max_blocks || state->pool_used >= state->max_blocks) {
            return false;
        }

        new_blk.values = &state->data_pool[state->pool_used * state->block_capacity];
        new_blk.len = right_len;
        memcpy(new_blk.values, right_src, right_len * sizeof(new_blk.values[0]));
        state->pool_used += 1;

        for (i = state->block_count; i > idx + 1; --i) {
            state->blocks[i] = state->blocks[i - 1];
        }
        state->blocks[idx + 1] = new_blk;
        blk = &state->blocks[idx];
        blk->len = left_len;
        state->block_count += 1;
    }

    block_rebuild_prefix_from(state, idx);
    return true;
}
