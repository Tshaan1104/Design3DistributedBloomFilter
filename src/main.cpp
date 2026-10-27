#include "design3/DistributedSimulation.h"
#include "design3/Workload.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace design3;

namespace {

struct CliOptions {
  std::size_t nodes{2};
  std::size_t partitions{0};
  std::size_t bitCount{1 << 20};
  std::size_t hashes{5};
  std::size_t keysPerNode{5000};
  std::size_t probeKeys{50000};
  std::uint64_t seed{42};
  std::string mode{"parallel"};
  std::string workload{"sample"};
  std::string dataDir{"../data"};
};

std::size_t parseSize(const std::string &value, const std::string &name) {
  try {
    return static_cast<std::size_t>(std::stoull(value));
  } catch (...) {
    throw std::invalid_argument("Invalid value for " + name + ": " + value);
  }
}

CliOptions parseArgs(int argc, char **argv) {
  CliOptions opts;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto requireValue = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        throw std::invalid_argument("Missing value for " + name);
      }
      return argv[++i];
    };

    if (arg == "--nodes") {
      opts.nodes = parseSize(requireValue(arg), arg);
    } else if (arg == "--partitions") {
      opts.partitions = parseSize(requireValue(arg), arg);
    } else if (arg == "--bit-count") {
      opts.bitCount = parseSize(requireValue(arg), arg);
    } else if (arg == "--hashes") {
      opts.hashes = parseSize(requireValue(arg), arg);
    } else if (arg == "--keys-per-node") {
      opts.keysPerNode = parseSize(requireValue(arg), arg);
    } else if (arg == "--probe-keys") {
      opts.probeKeys = parseSize(requireValue(arg), arg);
    } else if (arg == "--seed") {
      opts.seed = static_cast<std::uint64_t>(parseSize(requireValue(arg), arg));
    } else if (arg == "--mode") {
      opts.mode = requireValue(arg);
    } else if (arg == "--workload") {
      opts.workload = requireValue(arg);
    } else if (arg == "--data-dir") {
      opts.dataDir = requireValue(arg);
    } else if (arg == "--help") {
      std::cout
          << "Usage: design3_demo [options]\n"
          << "  --nodes <N>             Node count\n"
          << "  --partitions <P>        Partition count (default: nodes)\n"
          << "  --mode <sequential|parallel>\n"
          << "  --keys-per-node <K>     Build keys per node (synthetic)\n"
          << "  --probe-keys <K>        Probe keys count (synthetic)\n"
          << "  --seed <S>              RNG seed\n"
          << "  --workload <sample|synthetic>\n"
          << "  --data-dir <path>       Directory with sample CSV files\n"
          << "  --bit-count <bits>      Bloom filter bit count\n"
          << "  --hashes <count>        Number of hash functions\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("Unknown argument: " + arg);
    }
  }

  if (opts.partitions == 0) {
    opts.partitions = opts.nodes;
  }
  return opts;
}

void printDatasetConfig() {
  const auto cfg = defaultSsbSf1Config();
  std::cout << "dataset:\n"
            << "  bucket: " << cfg.bucket << "\n"
            << "  schema: " << cfg.schema << "\n"
            << "  tables: ";
  for (std::size_t i = 0; i < cfg.tables.size(); ++i) {
    std::cout << cfg.tables[i] << (i + 1 == cfg.tables.size() ? "\n" : ", ");
  }
  std::cout << "  join: " << cfg.relevantJoin << "\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto opts = parseArgs(argc, argv);
    if (opts.nodes == 0 || opts.partitions == 0 || opts.bitCount == 0 || opts.hashes == 0) {
      throw std::invalid_argument("nodes/partitions/bit-count/hashes must be > 0");
    }

    printDatasetConfig();

    WorkloadData workload;
    if (opts.workload == "sample") {
      workload = sampleSsbPartLineorderWorkload(opts.dataDir);
    } else if (opts.workload == "synthetic") {
      workload = syntheticSsbPartLineorderWorkload(opts.nodes * opts.keysPerNode, opts.probeKeys, opts.seed);
    } else {
      throw std::invalid_argument("workload must be sample or synthetic");
    }

    std::vector<std::vector<std::uint64_t>> buildPerNode(opts.nodes);
    for (std::size_t i = 0; i < workload.partKeysBuild.size(); ++i) {
      buildPerNode[i % opts.nodes].push_back(workload.partKeysBuild[i]);
    }

    SimulationConfig cfg;
    cfg.nodeCount = opts.nodes;
    cfg.partitionCount = opts.partitions;
    cfg.bitCount = opts.bitCount;
    cfg.hashFunctionCount = opts.hashes;
    cfg.mergeMode = (opts.mode == "sequential") ? MergeMode::Sequential : MergeMode::Parallel;

    const auto result = runDistributedSimulation(cfg, buildPerNode, workload.lineorderKeysProbe);

    std::cout << "metrics:\n"
              << "  node_count: " << result.metrics.nodeCount << "\n"
              << "  partition_count: " << result.metrics.partitionCount << "\n"
              << "  build_key_count: " << result.metrics.buildKeyCount << "\n"
              << "  probe_key_count: " << result.metrics.probeKeyCount << "\n"
              << "  local_filter_build_time_ms: " << result.metrics.buildTimeMs << "\n"
              << "  split_exchange_time_ms: " << result.metrics.splitExchangeTimeMs << "\n"
              << "  merge_time_ms: " << result.metrics.mergeTimeMs << "\n"
              << "  concat_time_ms: " << result.metrics.concatTimeMs << "\n"
              << "  probe_filter_time_ms: " << result.metrics.probeTimeMs << "\n"
              << "  candidate_count_after_filtering: " << result.metrics.candidateCount << "\n"
              << "  false_negatives: " << result.metrics.falseNegatives << "\n"
              << "  estimated_false_positives: " << result.metrics.falsePositives << "\n";

    if (result.metrics.falseNegatives > 0) {
      std::cerr << "Validation failed: false negatives detected\n";
      return 2;
    }

    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
}
