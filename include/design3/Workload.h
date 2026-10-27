#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace design3 {

struct DatasetConfig {
  std::string bucket;
  std::string schema;
  std::vector<std::string> tables;
  std::string relevantJoin;
};

struct WorkloadData {
  std::vector<std::uint64_t> partKeysBuild;
  std::vector<std::uint64_t> lineorderKeysProbe;
};

DatasetConfig defaultSsbSf1Config();
WorkloadData sampleSsbPartLineorderWorkload(const std::string &baseDir);
WorkloadData syntheticSsbPartLineorderWorkload(std::size_t partKeyCount,
                                               std::size_t probeKeyCount,
                                               std::uint64_t seed);

} // namespace design3
