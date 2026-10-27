#include "design3/DistributedSimulation.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace design3 {

namespace {
using Clock = std::chrono::steady_clock;

double elapsedMs(const Clock::time_point &start, const Clock::time_point &end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::size_t effectiveThreads(std::size_t tasks) {
  const std::size_t hw = std::max(1u, std::thread::hardware_concurrency());
  return std::max<std::size_t>(1, std::min(tasks, hw));
}

BloomFilter mergePartitionFromSources(const std::vector<BloomFilterPartition> &parts) {
  if (parts.empty()) {
    throw std::invalid_argument("No source partitions to merge");
  }
  BloomFilter merged = BloomFilter::fromBytes(parts.front().partitionBitCount,
                                              parts.front().hashFunctionCount,
                                              parts.front().bytes);
  for (std::size_t i = 1; i < parts.size(); ++i) {
    merged.mergeOr(BloomFilter::fromBytes(parts[i].partitionBitCount,
                                          parts[i].hashFunctionCount,
                                          parts[i].bytes));
  }
  return merged;
}

} // namespace

SimulationResult runDistributedSimulation(const SimulationConfig &config,
                                          const std::vector<std::vector<std::uint64_t>> &buildKeysPerNode,
                                          const std::vector<std::uint64_t> &probeKeys) {
  if (config.nodeCount == 0 || config.partitionCount == 0) {
    throw std::invalid_argument("nodeCount and partitionCount must be > 0");
  }
  if (buildKeysPerNode.size() != config.nodeCount) {
    throw std::invalid_argument("buildKeysPerNode size must equal nodeCount");
  }

  SimulationResult result;
  result.metrics.nodeCount = config.nodeCount;
  result.metrics.partitionCount = config.partitionCount;
  result.metrics.probeKeyCount = probeKeys.size();

  std::size_t totalBuild = 0;
  for (const auto &keys: buildKeysPerNode) {
    totalBuild += keys.size();
  }
  result.metrics.buildKeyCount = totalBuild;

  const auto buildStart = Clock::now();
  std::vector<BloomFilter> localFilters;
  localFilters.reserve(config.nodeCount);
  for (std::size_t node = 0; node < config.nodeCount; ++node) {
    BloomFilter filter(config.bitCount, config.hashFunctionCount);
    for (auto key: buildKeysPerNode[node]) {
      filter.insert(key);
    }
    localFilters.push_back(std::move(filter));
  }
  const auto buildEnd = Clock::now();
  result.metrics.buildTimeMs = elapsedMs(buildStart, buildEnd);

  const auto splitStart = Clock::now();
  std::vector<std::vector<BloomFilterPartition>> splitBySource;
  splitBySource.reserve(config.nodeCount);
  for (const auto &f: localFilters) {
    splitBySource.push_back(f.split(config.partitionCount));
  }

  // Design 3 exchange model: source i sends partition p to destination owner (p % nodeCount).
  // Corresponding partitions from all sources represent the same global bit-range,
  // so they can be OR-merged safely at the owning destination.
  std::vector<std::vector<BloomFilterPartition>> incomingByPartition(config.partitionCount);
  for (std::size_t src = 0; src < config.nodeCount; ++src) {
    for (std::size_t p = 0; p < config.partitionCount; ++p) {
      const auto message = localFilters[src].toMessage(splitBySource[src][p]);
      incomingByPartition[p].push_back(BloomFilter::partitionFromMessage(message));
    }
  }
  const auto splitEnd = Clock::now();
  result.metrics.splitExchangeTimeMs = elapsedMs(splitStart, splitEnd);

  const auto mergeStart = Clock::now();
  std::vector<BloomFilterPartition> mergedPartitions(config.partitionCount);
  if (config.mergeMode == MergeMode::Sequential || config.partitionCount == 1) {
    for (std::size_t p = 0; p < config.partitionCount; ++p) {
      const auto merged = mergePartitionFromSources(incomingByPartition[p]);
      auto out = incomingByPartition[p].front();
      out.bytes = merged.bytes();
      mergedPartitions[p] = std::move(out);
    }
  } else {
    const std::size_t workers = effectiveThreads(config.partitionCount);
    std::vector<std::future<void>> futures;
    futures.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
      futures.emplace_back(std::async(std::launch::async, [&, worker] {
        for (std::size_t p = worker; p < config.partitionCount; p += workers) {
          const auto merged = mergePartitionFromSources(incomingByPartition[p]);
          auto out = incomingByPartition[p].front();
          out.bytes = merged.bytes();
          mergedPartitions[p] = std::move(out);
        }
      }));
    }
    for (auto &f: futures) {
      f.get();
    }
  }
  const auto mergeEnd = Clock::now();
  result.metrics.mergeTimeMs = elapsedMs(mergeStart, mergeEnd);

  const auto concatStart = Clock::now();
  BloomFilter global = BloomFilter::concat(mergedPartitions);
  result.globalFiltersByNode.assign(config.nodeCount, global);
  const auto concatEnd = Clock::now();
  result.metrics.concatTimeMs = elapsedMs(concatStart, concatEnd);

  std::unordered_set<std::uint64_t> buildSet;
  buildSet.reserve(totalBuild * 2 + 1);
  for (const auto &keys: buildKeysPerNode) {
    for (auto key: keys) {
      buildSet.insert(key);
    }
  }

  const auto probeStart = Clock::now();
  std::size_t candidates = 0;
  std::size_t falseNegatives = 0;
  std::size_t falsePositives = 0;

  const auto &referenceGlobal = result.globalFiltersByNode.front();
  for (auto key: probeKeys) {
    const bool candidate = referenceGlobal.contains(key);
    if (candidate) {
      ++candidates;
      if (buildSet.find(key) == buildSet.end()) {
        ++falsePositives;
      }
    }
  }

  for (auto key: buildSet) {
    if (!referenceGlobal.contains(key)) {
      ++falseNegatives;
    }
  }

  const auto probeEnd = Clock::now();
  result.metrics.probeTimeMs = elapsedMs(probeStart, probeEnd);
  result.metrics.candidateCount = candidates;
  result.metrics.falseNegatives = falseNegatives;
  result.metrics.falsePositives = falsePositives;

  return result;
}

} // namespace design3
