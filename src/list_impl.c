#include "list_impl.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

void list_insert(ListState *list, size_t pos, int64_t value) {
    Node *node = NULL;
    Node *prev = NULL;
    size_t i = 0;

    if (pos > list->size || list->pool_used >= list->pool_capacity) {
        fprintf(stderr, "List insert out of range or pool exhausted\n");
        exit(1);
    }

    node = &list->pool[list->pool_used++];
    node->value = value;

    if (pos == 0) {
        node->next = list->head;
        list->head = node;
        list->size += 1;
        return;
    }

    prev = list->head;
    for (i = 0; i < pos - 1; ++i) {
        prev = prev->next;
    }

    node->next = prev->next;
    prev->next = node;
    list->size += 1;
}

bool verify_list_vs_block(const ListState *list, const BlockListState *block) {
    Node *cur = NULL;
    size_t bi = 0;
    size_t bj = 0;
    size_t idx = 0;

    if (list == NULL || block == NULL) {
        return false;
    }
    if (list->size != block->total_size) {
        fprintf(
            stderr,
            "Size mismatch: list=%zu, block=%zu\n",
            list->size,
            block->total_size
        );
        return false;
    }

    cur = list->head;
    for (idx = 0; idx < list->size; ++idx) {
        while (bi < block->block_count && bj >= block->blocks[bi].len) {
            bi += 1;
            bj = 0;
        }
        if (cur == NULL || bi >= block->block_count) {
            fprintf(stderr, "Sequence ended early at index %zu\n", idx);
            return false;
        }
        if (cur->value != block->blocks[bi].values[bj]) {
            fprintf(
                stderr,
                "Value mismatch at index %zu: list=%" PRId64 ", block=%" PRId64 "\n",
                idx,
                cur->value,
                block->blocks[bi].values[bj]
            );
            return false;
        }
        cur = cur->next;
        bj += 1;
    }
    return cur == NULL;
}
