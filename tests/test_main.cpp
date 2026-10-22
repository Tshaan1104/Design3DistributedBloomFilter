#include "design3/BloomFilter.h"
#include "design3/DistributedSimulation.h"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace design3;

namespace {

void expect(bool cond, const std::string &msg) {
  if (!cond) {
    throw std::runtime_error(msg);
  }
}

void expectThrows(const std::function<void()> &fn, const std::string &msg) {
  try {
    fn();
  } catch (...) {
    return;
  }
  throw std::runtime_error(msg);
}

void testInsertContains() {
  BloomFilter bf(1024, 4);
  bf.insert(123);
  bf.insert(456);
  expect(bf.contains(123), "contains(123) expected true");
  expect(bf.contains(456), "contains(456) expected true");
}

void testMergeCompatibility() {
  BloomFilter a(1024, 3);
  BloomFilter b(1024, 3);
  BloomFilter c(2048, 3);
  a.insert(10);
  b.insert(20);
  a.mergeOr(b);
  expect(a.contains(10) && a.contains(20), "merged filter should contain both keys");
  expectThrows([&]() { a.mergeOr(c); }, "expected incompatible merge to throw");
}

void testSplitConcatRoundTripUneven() {
  BloomFilter bf(1031, 4);
  for (int i = 0; i < 100; ++i) {
    bf.insert(static_cast<std::uint64_t>(i * 17));
  }
  auto partitions = bf.split(7);
  auto roundTrip = BloomFilter::concat(partitions);
  for (int i = 0; i < 100; ++i) {
    expect(roundTrip.contains(static_cast<std::uint64_t>(i * 17)), "round-trip lost key");
  }

  auto msg = bf.toMessage(partitions.front()).serialize();
  auto deser = BloomPartitionMessage::deserialize(msg);
  auto part = BloomFilter::partitionFromMessage(deser);
  expect(part.partitionBitCount == partitions.front().partitionBitCount, "message round-trip mismatch");
}

void testDistributedSequentialAndParallel() {
  const std::vector<std::vector<std::uint64_t>> build1{{1, 2, 3, 100}, {3, 4, 5}, {7}, {8, 9, 10}};
  const std::vector<std::uint64_t> probe{1, 2, 6, 7, 9, 11, 100, 200};

  for (std::size_t nodes: {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
    std::vector<std::vector<std::uint64_t>> build(nodes);
    for (std::size_t i = 0; i < build1.size(); ++i) {
      for (auto key: build1[i]) {
        build[i % nodes].push_back(key);
      }
    }

    for (auto mode: {MergeMode::Sequential, MergeMode::Parallel}) {
      SimulationConfig cfg;
      cfg.nodeCount = nodes;
      cfg.partitionCount = nodes;
      cfg.bitCount = 4096;
      cfg.hashFunctionCount = 4;
      cfg.mergeMode = mode;

      const auto result = runDistributedSimulation(cfg, build, probe);
      expect(result.metrics.falseNegatives == 0, "false negatives detected");
      expect(result.globalFiltersByNode.size() == nodes, "global filters per node mismatch");

      for (std::size_t i = 1; i < result.globalFiltersByNode.size(); ++i) {
        expect(result.globalFiltersByNode[i].bytes() == result.globalFiltersByNode[0].bytes(),
               "global filters differ across nodes");
      }
    }
  }
}

void testUnevenPartitionsAndEmptyNodeInput() {
  SimulationConfig cfg;
  cfg.nodeCount = 4;
  cfg.partitionCount = 3;
  cfg.bitCount = 2053;
  cfg.hashFunctionCount = 5;
  cfg.mergeMode = MergeMode::Parallel;

  std::vector<std::vector<std::uint64_t>> build{{1, 2, 3}, {}, {7, 8}, {9}};
  std::vector<std::uint64_t> probe{1, 2, 3, 4, 5, 7, 8, 9, 10};
  const auto result = runDistributedSimulation(cfg, build, probe);
  expect(result.metrics.falseNegatives == 0, "false negatives with uneven/empty input");
}

} // namespace

int main() {
  try {
    testInsertContains();
    testMergeCompatibility();
    testSplitConcatRoundTripUneven();
    testDistributedSequentialAndParallel();
    testUnevenPartitionsAndEmptyNodeInput();
    std::cout << "All tests passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Test failure: " << ex.what() << "\n";
    return 1;
  }
}
