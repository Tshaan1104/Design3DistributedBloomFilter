#include "design3/Workload.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace design3 {

namespace {
std::vector<std::uint64_t> loadSingleColumnCsv(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }

  std::vector<std::uint64_t> values;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    if (first) {
      first = false;
      if (!std::isdigit(static_cast<unsigned char>(line[0]))) {
        continue;
      }
    }
    std::istringstream iss(line);
    std::string token;
    if (!std::getline(iss, token, ',')) {
      continue;
    }
    values.push_back(static_cast<std::uint64_t>(std::stoull(token)));
  }
  return values;
}
} // namespace

DatasetConfig defaultSsbSf1Config() {
  return DatasetConfig{
      "flexpushdowndb",
      "ssb-sf1-sortlineorder/csv/",
      {"lineorder", "part", "customer", "supplier", "date"},
      "lineorder.lo_partkey = part.p_partkey"};
}

WorkloadData sampleSsbPartLineorderWorkload(const std::string &baseDir) {
  WorkloadData out;
  out.partKeysBuild = loadSingleColumnCsv(baseDir + "/ssb_sf1_sample_part.csv");
  out.lineorderKeysProbe = loadSingleColumnCsv(baseDir + "/ssb_sf1_sample_lineorder.csv");

  if (!out.partKeysBuild.empty() && !out.lineorderKeysProbe.empty()) {
    return out;
  }

  out.partKeysBuild = {1, 2, 3, 7, 11, 19, 23, 42};
  out.lineorderKeysProbe = {2, 3, 4, 7, 9, 19, 40, 42, 100};
  return out;
}

WorkloadData syntheticSsbPartLineorderWorkload(std::size_t partKeyCount,
                                               std::size_t probeKeyCount,
                                               std::uint64_t seed) {
  if (partKeyCount == 0) {
    throw std::invalid_argument("partKeyCount must be > 0");
  }

  WorkloadData out;
  out.partKeysBuild.reserve(partKeyCount);
  for (std::size_t i = 0; i < partKeyCount; ++i) {
    out.partKeysBuild.push_back(static_cast<std::uint64_t>(i + 1));
  }

  std::mt19937_64 gen(seed);
  std::uniform_int_distribution<std::uint64_t> hitDist(1, static_cast<std::uint64_t>(partKeyCount));
  std::uniform_int_distribution<std::uint64_t> missDist(static_cast<std::uint64_t>(partKeyCount + 1),
                                                        static_cast<std::uint64_t>(partKeyCount * 20 + 100));

  out.lineorderKeysProbe.reserve(probeKeyCount);
  for (std::size_t i = 0; i < probeKeyCount; ++i) {
    const bool chooseHit = (i % 10) < 8; // Approximate lineorder being mostly joinable with part keys.
    out.lineorderKeysProbe.push_back(chooseHit ? hitDist(gen) : missDist(gen));
  }

  return out;
}

} // namespace design3
