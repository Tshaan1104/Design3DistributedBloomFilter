# Design3DistributedBloomFilter

Standalone C++ implementation of **Design 3: distributed Bloom-filter merge**, modeled after FlexPushdownDB's distributed Bloom-filter pipeline but without Arrow/CAF/AWS dependencies.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Run demo (`lineorder.lo_partkey = part.p_partkey`)

Sequential:

```bash
./build/design3_demo --workload sample --nodes 2 --partitions 2 --mode sequential
```

Parallel:

```bash
./build/design3_demo --workload synthetic --nodes 4 --partitions 4 --mode parallel --keys-per-node 50000 --probe-keys 200000 --seed 7
```

## SSB SF1 mapping

Dataset config (`/config/ssb_sf1_dataset.conf`):
- bucket: `flexpushdowndb`
- schema: `ssb-sf1-sortlineorder/csv/`
- tables: `lineorder`, `part`, `customer`, `supplier`, `date`
- join: `lineorder.lo_partkey = part.p_partkey`

This repository includes deterministic sample CSV files in `/data` and synthetic generation for larger runs. Unit tests do **not** download S3 data.

## Operator mapping to FlexPushdownDB

- `DistGlobalBloomFilterInitPOp` -> `SimulationConfig` global Bloom-filter layout (`bitCount`, `hashFunctionCount`, partitioning)
- `GlobalBloomFilterInit/Finalize` -> local filter construction on each node
- `BloomFilterSplitPOp` -> `BloomFilter::split`
- `DistGlobalBloomFilterMergePOp` -> per-partition OR merge over all exchanged source partitions
- `BloomFilterConcatPOp` -> `BloomFilter::concat`
- `BloomFilterUsePOp` -> probe-side filtering in `runDistributedSimulation`

## Design 3 simulation notes

Pipeline implemented in-process:
1. Build local Bloom filter on each node from local `part.p_partkey` keys.
2. Split each local filter into partitions.
3. Simulate all-to-all partition exchange.
4. Merge corresponding partitions with bitwise OR (sequential or parallel mode).
5. Concatenate merged partitions to reconstruct global Bloom filter on every node.
6. Filter probe keys (`lineorder.lo_partkey`) with the reconstructed global filter.

Why corresponding partitions can be OR-merged: each partition represents the same global bit-range across all local filters; OR is exactly the Bloom-filter union operation for aligned layouts.

## CLI options

`design3_demo` supports:
- `--nodes`
- `--partitions` (defaults to node count)
- `--mode sequential|parallel`
- `--keys-per-node` (synthetic workload)
- `--probe-keys` (synthetic workload)
- `--seed`
- `--workload sample|synthetic`
- `--bit-count`
- `--hashes`
- `--data-dir`

Printed metrics:
- node count / partition count
- build/probe key counts
- local build time
- split+exchange time
- merge time
- concat time
- probe/filter time
- candidate count after filtering
- false negatives
- estimated false positives

The executable returns non-zero when correctness validation fails (false negatives > 0).

## Validation checklist

- [x] Build with CMake
- [x] Run tests with CTest
- [x] Run sequential demo
- [x] Run parallel demo
- [x] Verify no false negatives for union of build keys
