#pragma once

#include "design3/BloomFilter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace design3 {

enum class MergeMode {
  Sequential,
  Parallel
};

struct SimulationConfig {
  std::size_t nodeCount{1};
  std::size_t partitionCount{1};
  std::size_t bitCount{1 << 20};
  std::size_t hashFunctionCount{5};
  MergeMode mergeMode{MergeMode::Sequential};
};

struct SimulationMetrics {
  std::size_t nodeCount{};
  std::size_t partitionCount{};
  std::size_t buildKeyCount{};
  std::size_t probeKeyCount{};
  double buildTimeMs{};
  double splitExchangeTimeMs{};
  double mergeTimeMs{};
  double concatTimeMs{};
  double probeTimeMs{};
  std::size_t candidateCount{};
  std::size_t falseNegatives{};
  std::size_t falsePositives{};
};

struct SimulationResult {
  SimulationMetrics metrics;
  std::vector<BloomFilter> globalFiltersByNode;
};

SimulationResult runDistributedSimulation(const SimulationConfig &config,
                                          const std::vector<std::vector<std::uint64_t>> &buildKeysPerNode,
                                          const std::vector<std::uint64_t> &probeKeys);

} // namespace design3
