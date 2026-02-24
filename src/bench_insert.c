#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "array_impl.h"
#include "blocklist_impl.h"
#include "list_impl.h"
#include "utils.h"

#define PROGRESS_UPDATE_INTERVAL_NS 200000000ULL
#define PHASE_LABEL_LEN 64

typedef struct {
    const char *output_path;
    size_t n;
    size_t block_base_len;
    bool show_progress;

    BenchMode mode;
    bool run_array;
    bool run_list;
    bool run_block;

    unsigned int *seed_list;
    size_t run_count;

    size_t total_points;
    size_t *positions;

    uint64_t *array_times;
    uint64_t *list_times;
    uint64_t *block_times;

    uint64_t *array_avg_times;
    uint64_t *list_avg_times;
    uint64_t *block_avg_times;

    ArrayState array;
    ListState list;
    BlockListState block;

    SummaryStats array_summary;
    SummaryStats list_summary;
    SummaryStats block_summary;
} BenchContext;

static void free_context(BenchContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    free(ctx->positions);
    free(ctx->array_times);
    free(ctx->list_times);
    free(ctx->block_times);
    free(ctx->array_avg_times);
    free(ctx->list_avg_times);
    free(ctx->block_avg_times);
    free(ctx->seed_list);
    free(ctx->array.data);
    free(ctx->list.pool);
    block_list_free(&ctx->block);
}

static bool init_context(BenchContext *ctx) {
    if (ctx->n > 0 && ctx->run_count > SIZE_MAX / ctx->n) {
        fprintf(stderr, "N * run_count overflow\n");
        return false;
    }
    ctx->total_points = ctx->n * ctx->run_count;

    ctx->positions = checked_malloc_array(ctx->total_points, sizeof(ctx->positions[0]), "positions");
    if (ctx->run_array) {
        ctx->array_times = checked_malloc_array(
            ctx->total_points,
            sizeof(ctx->array_times[0]),
            "array_times"
        );
        ctx->array_avg_times = checked_malloc_array(
            ctx->n,
            sizeof(ctx->array_avg_times[0]),
            "array_avg_times"
        );
        ctx->array.data = checked_malloc_array(ctx->n, sizeof(ctx->array.data[0]), "array data");
    }
    if (ctx->run_list) {
        ctx->list_times = checked_malloc_array(
            ctx->total_points,
            sizeof(ctx->list_times[0]),
            "list_times"
        );
        ctx->list_avg_times = checked_malloc_array(
            ctx->n,
            sizeof(ctx->list_avg_times[0]),
            "list_avg_times"
        );
        ctx->list.pool = checked_malloc_array(ctx->n, sizeof(ctx->list.pool[0]), "list pool");
    }
    if (ctx->run_block) {
        ctx->block_times = checked_malloc_array(
            ctx->total_points,
            sizeof(ctx->block_times[0]),
            "block_times"
        );
        ctx->block_avg_times = checked_malloc_array(
            ctx->n,
            sizeof(ctx->block_avg_times[0]),
            "block_avg_times"
        );
    }

    if (ctx->positions == NULL ||
        (ctx->run_array &&
         (ctx->array_times == NULL || ctx->array_avg_times == NULL || ctx->array.data == NULL)) ||
        (ctx->run_list &&
         (ctx->list_times == NULL || ctx->list_avg_times == NULL || ctx->list.pool == NULL)) ||
        (ctx->run_block && (ctx->block_times == NULL || ctx->block_avg_times == NULL))) {
        return false;
    }

    if (ctx->run_array) {
        ctx->array.capacity = ctx->n;
    }
    if (ctx->run_list) {
        ctx->list.pool_capacity = ctx->n;
    }
    if (ctx->run_block && !block_list_init(&ctx->block, ctx->n, ctx->block_base_len)) {
        fprintf(stderr, "Failed to initialize block list (L=%zu)\n", ctx->block_base_len);
        return false;
    }

    return true;
}

static void build_phase_label(
    char *phase_label,
    size_t phase_label_len,
    const char *base,
    size_t run,
    size_t run_count
) {
    if (run_count > 1) {
        snprintf(phase_label, phase_label_len, "%s %zu/%zu", base, run + 1, run_count);
    } else {
        snprintf(phase_label, phase_label_len, "%s", base);
    }
}

static bool begin_progress(
    const BenchContext *ctx,
    const char *phase_label,
    struct timespec *phase_start,
    uint64_t *last_progress_ns
) {
    if (!ctx->show_progress) {
        return true;
    }
    if (clock_gettime(CLOCK_MONOTONIC_RAW, phase_start) != 0) {
        perror("clock_gettime");
        return false;
    }
    *last_progress_ns = timespec_to_ns(phase_start);
    print_progress(phase_label, 0, ctx->n, phase_start, phase_start);
    return true;
}

static bool update_progress(
    const BenchContext *ctx,
    const char *phase_label,
    size_t i,
    size_t stride,
    const struct timespec *phase_start,
    struct timespec *phase_now,
    uint64_t *last_progress_ns
) {
    uint64_t now_ns = 0;

    if (!ctx->show_progress) {
        return true;
    }
    if (i != ctx->n && (i % stride) != 0) {
        return true;
    }

    if (clock_gettime(CLOCK_MONOTONIC_RAW, phase_now) != 0) {
        perror("clock_gettime");
        return false;
    }

    now_ns = timespec_to_ns(phase_now);
    if (i == ctx->n || now_ns - *last_progress_ns >= PROGRESS_UPDATE_INTERVAL_NS) {
        print_progress(phase_label, i, ctx->n, phase_start, phase_now);
        *last_progress_ns = now_ns;
    }
    return true;
}

static bool run_generate_positions(BenchContext *ctx, size_t run, size_t stride) {
    size_t i = 0;
    size_t base = run * ctx->n;
    unsigned int rng_state = ctx->seed_list[run];
    char phase_label[PHASE_LABEL_LEN];
    struct timespec phase_start = {0};
    struct timespec phase_now = {0};
    uint64_t last_progress_ns = 0;

    build_phase_label(phase_label, sizeof(phase_label), "gen-pos", run, ctx->run_count);
    if (!begin_progress(ctx, phase_label, &phase_start, &last_progress_ns)) {
        return false;
    }

    for (i = 1; i <= ctx->n; ++i) {
        ctx->positions[base + i - 1] = (size_t)(rand_r(&rng_state) % i);
        if (!update_progress(
                ctx,
                phase_label,
                i,
                stride,
                &phase_start,
                &phase_now,
                &last_progress_ns
            )) {
            return false;
        }
    }

    return true;
}

static bool run_array_once(BenchContext *ctx, size_t run, size_t stride) {
    size_t i = 0;
    size_t base = run * ctx->n;
    char phase_label[PHASE_LABEL_LEN];
    struct timespec phase_start = {0};
    struct timespec phase_now = {0};
    struct timespec t0 = {0};
    struct timespec t1 = {0};
    uint64_t last_progress_ns = 0;

    ctx->array.size = 0;
    build_phase_label(phase_label, sizeof(phase_label), "insert-array", run, ctx->run_count);
    if (!begin_progress(ctx, phase_label, &phase_start, &last_progress_ns)) {
        return false;
    }

    for (i = 1; i <= ctx->n; ++i) {
        size_t pos = ctx->positions[base + i - 1];
        int64_t value = (int64_t)i;

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t0) != 0) {
            perror("clock_gettime");
            return false;
        }
        array_insert(&ctx->array, pos, value);
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t1) != 0) {
            perror("clock_gettime");
            return false;
        }
        ctx->array_times[base + i - 1] = diff_ns(&t0, &t1);

        if (!update_progress(
                ctx,
                phase_label,
                i,
                stride,
                &phase_start,
                &phase_now,
                &last_progress_ns
            )) {
            return false;
        }
    }

    if (ctx->array.size != ctx->n) {
        fprintf(
            stderr,
            "Array size mismatch after run: expected=%zu got=%zu\n",
            ctx->n,
            ctx->array.size
        );
        return false;
    }

    return true;
}

static bool run_list_once(BenchContext *ctx, size_t run, size_t stride) {
    size_t i = 0;
    size_t base = run * ctx->n;
    char phase_label[PHASE_LABEL_LEN];
    struct timespec phase_start = {0};
    struct timespec phase_now = {0};
    struct timespec t0 = {0};
    struct timespec t1 = {0};
    uint64_t last_progress_ns = 0;

    ctx->list.head = NULL;
    ctx->list.size = 0;
    ctx->list.pool_used = 0;

    build_phase_label(phase_label, sizeof(phase_label), "insert-list", run, ctx->run_count);
    if (!begin_progress(ctx, phase_label, &phase_start, &last_progress_ns)) {
        return false;
    }

    for (i = 1; i <= ctx->n; ++i) {
        size_t pos = ctx->positions[base + i - 1];
        int64_t value = (int64_t)i;

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t0) != 0) {
            perror("clock_gettime");
            return false;
        }
        list_insert(&ctx->list, pos, value);
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t1) != 0) {
            perror("clock_gettime");
            return false;
        }
        ctx->list_times[base + i - 1] = diff_ns(&t0, &t1);

        if (!update_progress(
                ctx,
                phase_label,
                i,
                stride,
                &phase_start,
                &phase_now,
                &last_progress_ns
            )) {
            return false;
        }
    }

    if (ctx->list.size != ctx->n) {
        fprintf(
            stderr,
            "List size mismatch after run: expected=%zu got=%zu\n",
            ctx->n,
            ctx->list.size
        );
        return false;
    }

    return true;
}

static bool run_block_once(BenchContext *ctx, size_t run, size_t stride) {
    size_t i = 0;
    size_t base = run * ctx->n;
    char phase_label[PHASE_LABEL_LEN];
    struct timespec phase_start = {0};
    struct timespec phase_now = {0};
    struct timespec t0 = {0};
    struct timespec t1 = {0};
    uint64_t last_progress_ns = 0;

    block_list_reset(&ctx->block);

    build_phase_label(phase_label, sizeof(phase_label), "insert-block", run, ctx->run_count);
    if (!begin_progress(ctx, phase_label, &phase_start, &last_progress_ns)) {
        return false;
    }

    for (i = 1; i <= ctx->n; ++i) {
        size_t pos = ctx->positions[base + i - 1];
        int64_t value = (int64_t)i;

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t0) != 0) {
            perror("clock_gettime");
            return false;
        }
        if (!block_list_insert(&ctx->block, pos, value)) {
            fprintf(stderr, "Block list insert failed at step %zu\n", i);
            return false;
        }
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t1) != 0) {
            perror("clock_gettime");
            return false;
        }
        ctx->block_times[base + i - 1] = diff_ns(&t0, &t1);

        if (!update_progress(
                ctx,
                phase_label,
                i,
                stride,
                &phase_start,
                &phase_now,
                &last_progress_ns
            )) {
            return false;
        }
    }

    if (ctx->block.total_size != ctx->n) {
        fprintf(
            stderr,
            "Block size mismatch after run: expected=%zu got=%zu\n",
            ctx->n,
            ctx->block.total_size
        );
        return false;
    }

    return true;
}

static bool verify_run_results(const BenchContext *ctx) {
    if (ctx->run_array && ctx->run_list && !verify_array_vs_list(&ctx->array, &ctx->list)) {
        return false;
    }
    if (ctx->run_array && ctx->run_block && !verify_array_vs_block(&ctx->array, &ctx->block)) {
        return false;
    }
    if (ctx->run_list && ctx->run_block && !verify_list_vs_block(&ctx->list, &ctx->block)) {
        return false;
    }
    return true;
}

static bool run_one_seed(BenchContext *ctx, size_t run, size_t stride) {
    if (!run_generate_positions(ctx, run, stride)) {
        return false;
    }
    if (ctx->run_array && !run_array_once(ctx, run, stride)) {
        return false;
    }
    if (ctx->run_list && !run_list_once(ctx, run, stride)) {
        return false;
    }
    if (ctx->run_block && !run_block_once(ctx, run, stride)) {
        return false;
    }
    return verify_run_results(ctx);
}

static bool write_csv_with_averages(BenchContext *ctx, size_t stride) {
    size_t i = 0;
    size_t run = 0;
    FILE *csv = NULL;
    char phase_label[PHASE_LABEL_LEN];
    struct timespec phase_start = {0};
    struct timespec phase_now = {0};
    uint64_t last_progress_ns = 0;

    csv = fopen(ctx->output_path, "w");
    if (csv == NULL) {
        fprintf(stderr, "Failed to open CSV output '%s': %s\n", ctx->output_path, strerror(errno));
        return false;
    }

    fprintf(csv, "step,value");
    for (run = 0; run < ctx->run_count; ++run) {
        fprintf(csv, ",pos_seed_%u", ctx->seed_list[run]);
        if (ctx->run_array) {
            fprintf(csv, ",array_ns_seed_%u", ctx->seed_list[run]);
        }
        if (ctx->run_list) {
            fprintf(csv, ",list_ns_seed_%u", ctx->seed_list[run]);
        }
        if (ctx->run_block) {
            fprintf(csv, ",block_ns_seed_%u", ctx->seed_list[run]);
        }
    }
    if (ctx->run_array) {
        fprintf(csv, ",array_ns_avg");
    }
    if (ctx->run_list) {
        fprintf(csv, ",list_ns_avg");
    }
    if (ctx->run_block) {
        fprintf(csv, ",block_ns_avg");
    }
    fputc('\n', csv);

    snprintf(phase_label, sizeof(phase_label), "write-csv");
    if (!begin_progress(ctx, phase_label, &phase_start, &last_progress_ns)) {
        fclose(csv);
        return false;
    }

    for (i = 1; i <= ctx->n; ++i) {
        long double array_sum = 0.0L;
        long double list_sum = 0.0L;
        long double block_sum = 0.0L;

        fprintf(csv, "%zu,%" PRId64, i, (int64_t)i);
        for (run = 0; run < ctx->run_count; ++run) {
            size_t idx = run * ctx->n + (i - 1);
            fprintf(csv, ",%zu", ctx->positions[idx]);
            if (ctx->run_array) {
                fprintf(csv, ",%" PRIu64, ctx->array_times[idx]);
                array_sum += (long double)ctx->array_times[idx];
            }
            if (ctx->run_list) {
                fprintf(csv, ",%" PRIu64, ctx->list_times[idx]);
                list_sum += (long double)ctx->list_times[idx];
            }
            if (ctx->run_block) {
                fprintf(csv, ",%" PRIu64, ctx->block_times[idx]);
                block_sum += (long double)ctx->block_times[idx];
            }
        }

        if (ctx->run_array) {
            uint64_t avg = (uint64_t)(array_sum / (long double)ctx->run_count + 0.5L);
            ctx->array_avg_times[i - 1] = avg;
            fprintf(csv, ",%" PRIu64, avg);
        }
        if (ctx->run_list) {
            uint64_t avg = (uint64_t)(list_sum / (long double)ctx->run_count + 0.5L);
            ctx->list_avg_times[i - 1] = avg;
            fprintf(csv, ",%" PRIu64, avg);
        }
        if (ctx->run_block) {
            uint64_t avg = (uint64_t)(block_sum / (long double)ctx->run_count + 0.5L);
            ctx->block_avg_times[i - 1] = avg;
            fprintf(csv, ",%" PRIu64, avg);
        }
        fputc('\n', csv);

        if (!update_progress(
                ctx,
                phase_label,
                i,
                stride,
                &phase_start,
                &phase_now,
                &last_progress_ns
            )) {
            fclose(csv);
            return false;
        }
    }

    fclose(csv);
    return true;
}

static bool build_summaries(BenchContext *ctx) {
    if (ctx->run_array && !build_summary(ctx->array_avg_times, ctx->n, &ctx->array_summary)) {
        return false;
    }
    if (ctx->run_list && !build_summary(ctx->list_avg_times, ctx->n, &ctx->list_summary)) {
        return false;
    }
    if (ctx->run_block && !build_summary(ctx->block_avg_times, ctx->n, &ctx->block_summary)) {
        return false;
    }
    return true;
}

static void print_run_overview(const BenchContext *ctx) {
    size_t run = 0;

    printf("N=%zu mode=%s runs=%zu seeds=", ctx->n, mode_to_string(ctx->mode), ctx->run_count);
    for (run = 0; run < ctx->run_count; ++run) {
        printf("%s%u", (run == 0) ? "" : ",", ctx->seed_list[run]);
    }
    if (ctx->run_block) {
        printf(" blockL=%zu", ctx->block_base_len);
    }
    printf(" output=%s\n", ctx->output_path);

    if (ctx->run_array) {
        print_summary_line("array(avg)", &ctx->array_summary);
    }
    if (ctx->run_list) {
        print_summary_line("list(avg)", &ctx->list_summary);
    }
    if (ctx->run_block) {
        print_summary_line("block(avg)", &ctx->block_summary);
    }
}

int main(int argc, char **argv) {
    BenchContext ctx = {0};
    const char *seed_list_arg = NULL;
    unsigned int default_seed = 123456789U;
    size_t stride = 1;
    size_t run = 0;
    int opt = 0;
    int ret = 1;

    static struct option long_opts[] = {
        {"n", required_argument, NULL, 'n'},
        {"seed", required_argument, NULL, 's'},
        {"seeds", required_argument, NULL, 'S'},
        {"output", required_argument, NULL, 'o'},
        {"mode", required_argument, NULL, 'm'},
        {"block-l", required_argument, NULL, 'L'},
        {"progress", no_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };

    ctx.output_path = "insert_bench.csv";
    ctx.n = 10000;
    ctx.block_base_len = 64;
    ctx.mode = MODE_BOTH;
    ctx.show_progress = false;

    while ((opt = getopt_long(argc, argv, "n:s:S:o:m:L:ph", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'n':
                if (!parse_size_t_arg(optarg, &ctx.n)) {
                    fprintf(stderr, "Invalid N: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 's':
                if (!parse_seed_arg(optarg, &default_seed)) {
                    fprintf(stderr, "Invalid seed: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'S':
                seed_list_arg = optarg;
                break;
            case 'o':
                ctx.output_path = optarg;
                break;
            case 'm':
                if (!parse_mode_arg(optarg, &ctx.mode)) {
                    fprintf(
                        stderr,
                        "Invalid mode: %s (expected: array|list|block|both|all)\n",
                        optarg
                    );
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'L':
                if (!parse_size_t_arg(optarg, &ctx.block_base_len)) {
                    fprintf(stderr, "Invalid block L: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'p':
                ctx.show_progress = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (seed_list_arg != NULL) {
        if (!parse_seed_list_arg(seed_list_arg, &ctx.seed_list, &ctx.run_count)) {
            fprintf(stderr, "Invalid seeds list: %s\n", seed_list_arg);
            print_usage(argv[0]);
            return 1;
        }
    } else {
        ctx.seed_list = checked_malloc_array(1, sizeof(ctx.seed_list[0]), "seed list");
        if (ctx.seed_list == NULL) {
            return 1;
        }
        ctx.seed_list[0] = default_seed;
        ctx.run_count = 1;
    }

    ctx.run_array = mode_has_array(ctx.mode);
    ctx.run_list = mode_has_list(ctx.mode);
    ctx.run_block = mode_has_block(ctx.mode);

    if (!init_context(&ctx)) {
        goto cleanup;
    }

    stride = progress_stride(ctx.n);
    for (run = 0; run < ctx.run_count; ++run) {
        if (!run_one_seed(&ctx, run, stride)) {
            goto cleanup;
        }
    }

    if (!write_csv_with_averages(&ctx, stride)) {
        goto cleanup;
    }

    if (!build_summaries(&ctx)) {
        goto cleanup;
    }

    print_run_overview(&ctx);
    ret = 0;

cleanup:
    free_context(&ctx);
    return ret;
}
