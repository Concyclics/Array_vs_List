#include "array_impl.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void array_insert(ArrayState *array, size_t pos, int64_t value) {
    size_t move_count = 0;

    if (pos > array->size || array->size >= array->capacity) {
        fprintf(stderr, "Array insert out of range or capacity exceeded\n");
        exit(1);
    }

    move_count = array->size - pos;
    if (move_count > 0) {
        memmove(
            &array->data[pos + 1],
            &array->data[pos],
            move_count * sizeof(array->data[0])
        );
    }

    array->data[pos] = value;
    array->size += 1;
}

bool verify_array_vs_list(const ArrayState *array, const ListState *list) {
    Node *cur = NULL;
    size_t idx = 0;

    if (array->size != list->size) {
        fprintf(
            stderr,
            "Size mismatch: array=%zu, list=%zu\n",
            array->size,
            list->size
        );
        return false;
    }

    cur = list->head;
    for (idx = 0; idx < array->size; ++idx) {
        if (cur == NULL) {
            fprintf(stderr, "List ended early at index %zu\n", idx);
            return false;
        }
        if (array->data[idx] != cur->value) {
            fprintf(
                stderr,
                "Value mismatch at index %zu: array=%" PRId64 ", list=%" PRId64 "\n",
                idx,
                array->data[idx],
                cur->value
            );
            return false;
        }
        cur = cur->next;
    }

    if (cur != NULL) {
        fprintf(stderr, "List has extra nodes after expected end\n");
        return false;
    }

    return true;
}

bool verify_array_vs_block(const ArrayState *array, const BlockListState *block) {
    size_t bi = 0;
    size_t bj = 0;
    size_t ai = 0;

    if (array == NULL || block == NULL) {
        return false;
    }
    if (array->size != block->total_size) {
        fprintf(
            stderr,
            "Size mismatch: array=%zu, block=%zu\n",
            array->size,
            block->total_size
        );
        return false;
    }

    for (ai = 0; ai < array->size; ++ai) {
        while (bi < block->block_count && bj >= block->blocks[bi].len) {
            bi += 1;
            bj = 0;
        }
        if (bi >= block->block_count) {
            fprintf(stderr, "Block list ended early at index %zu\n", ai);
            return false;
        }
        if (array->data[ai] != block->blocks[bi].values[bj]) {
            fprintf(
                stderr,
                "Value mismatch at index %zu: array=%" PRId64 ", block=%" PRId64 "\n",
                ai,
                array->data[ai],
                block->blocks[bi].values[bj]
            );
            return false;
        }
        bj += 1;
    }
    return true;
}
