# Random Insert Benchmark in C (`int64_t`)

This project benchmarks random-position insertion performance for:

- Preallocated array (`memmove` for shifting)
- Preallocated singly linked list (node pool, no per-insert `malloc`)

Values inserted are `1..N` as `int64_t`. For each insertion step, the program records:

- insertion position
- array insertion time in nanoseconds
- list insertion time in nanoseconds

Results are written to CSV, and a summary is printed to terminal.

## Directory Layout

- `src/`: C source and header files
- `data/`: benchmark CSV files and plotting notebook (`plot.ipynb`)

## Build

```bash
make
```

## Usage

```bash
./bench_insert [-n N] [-s SEED | -S seed1,seed2,...] [-o OUTPUT] [-m MODE] [-L BLOCK_L] [-p]
```

Options:

- `-n, --n <N>` number of insertions, default `10000`
- `-s, --seed <SEED>` single random seed, default `123456789`
- `-S, --seeds <LIST>` comma-separated seed list, e.g. `1,2,3`
- `-o, --output <CSV_PATH>` output CSV path, default `insert_bench.csv`
- `-m, --mode <MODE>` benchmark mode: `array`, `list`, `block`, `both`, or `all` (default `both`)
- `-L, --block-l <BLOCK_L>` block base length `L` for block list (`L..2L` split policy), default `64`
- `-p, --progress` show phase progress bar on `stderr` (optional)
- `-h, --help` show help

## CSV Format

Header depends on mode and seed list. For `-S 1,2,3`, examples:

```text
mode=both : step,value,pos_seed_1,array_ns_seed_1,list_ns_seed_1,pos_seed_2,array_ns_seed_2,list_ns_seed_2,pos_seed_3,array_ns_seed_3,list_ns_seed_3,array_ns_avg,list_ns_avg
mode=array: step,value,pos_seed_1,array_ns_seed_1,pos_seed_2,array_ns_seed_2,pos_seed_3,array_ns_seed_3,array_ns_avg
mode=list : step,value,pos_seed_1,list_ns_seed_1,pos_seed_2,list_ns_seed_2,pos_seed_3,list_ns_seed_3,list_ns_avg
mode=block: step,value,pos_seed_1,block_ns_seed_1,pos_seed_2,block_ns_seed_2,pos_seed_3,block_ns_seed_3,block_ns_avg
mode=all  : step,value,pos_seed_1,array_ns_seed_1,list_ns_seed_1,block_ns_seed_1,...,array_ns_avg,list_ns_avg,block_ns_avg
```

Each row is one insertion step. The final average column(s) are the mean across all provided seeds.

## Example

```bash
./bench_insert -n 10000 -s 123456789 -m array -o data/run_array.csv
./bench_insert -n 10000 -s 123456789 -m list -o data/run_list.csv
./bench_insert -n 10000 -s 123456789 -m both -o data/run_both.csv
./bench_insert -n 10000 -s 123456789 -m block -L 64 -o data/run_block.csv
./bench_insert -n 10000 -S 1,2,3 -m array -o data/run_array_multi.csv
./bench_insert -n 10000 -S 1,2,3 -m all -L 64 -o data/run_all_multi.csv
```

## Notes

- Random positions are pre-generated per seed and recorded in CSV as `pos_seed_*`.
- Insertion position range for step `i` is `[0, i-1]`, equivalent to current size `[0, size]`.
- Enabled methods are executed in separate phases (not interleaved), and final sequences are cross-checked for equality per seed.
- Block list keeps each block length in `[L, 2L]` (except transient overflow), and splits in half when insertion makes a block exceed `2L`.
- Block list maintains block prefix sums to locate the target block for position insertion with binary search.
