#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace design3 {

struct BloomFilterPartition {
  std::size_t partitionIndex{};
  std::size_t totalPartitions{};
  std::size_t startBit{};
  std::size_t originalBitCount{};
  std::size_t hashFunctionCount{};
  std::size_t partitionBitCount{};
  std::vector<std::uint8_t> bytes;
};

struct BloomPartitionMessage {
  std::size_t partitionIndex{};
  std::size_t totalPartitions{};
  std::size_t startBit{};
  std::size_t originalBitCount{};
  std::size_t hashFunctionCount{};
  std::size_t partitionBitCount{};
  std::vector<std::uint8_t> bytes;

  std::string serialize() const;
  static BloomPartitionMessage deserialize(const std::string &payload);
};

class BloomFilter {
public:
  BloomFilter(std::size_t bitCount, std::size_t hashFunctionCount);

  std::size_t bitCount() const noexcept { return bitCount_; }
  std::size_t hashFunctionCount() const noexcept { return hashFunctionCount_; }
  std::size_t byteCount() const noexcept { return bits_.size(); }
  const std::vector<std::uint8_t> &bytes() const noexcept { return bits_; }

  void insert(std::uint64_t key);
  bool contains(std::uint64_t key) const;

  void mergeOr(const BloomFilter &other);

  std::vector<BloomFilterPartition> split(std::size_t partitions) const;
  static BloomFilter concat(const std::vector<BloomFilterPartition> &partitions);

  BloomPartitionMessage toMessage(const BloomFilterPartition &partition) const;
  static BloomFilterPartition partitionFromMessage(const BloomPartitionMessage &message);

  static BloomFilter fromBytes(std::size_t bitCount,
                               std::size_t hashFunctionCount,
                               const std::vector<std::uint8_t> &bytes);

private:
  std::size_t bitCount_{};
  std::size_t hashFunctionCount_{};
  std::vector<std::uint8_t> bits_{};

  static std::uint64_t splitMix64(std::uint64_t x);
  std::uint64_t hashAt(std::uint64_t key, std::size_t hashIndex) const;

  void setBit(std::size_t idx);
  bool getBit(std::size_t idx) const;

  static std::size_t bytesForBits(std::size_t bitCount);
};

} // namespace design3
