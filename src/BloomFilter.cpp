#include "design3/BloomFilter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace design3 {

namespace {
constexpr std::uint64_t kSplitMixIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kMul1 = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t kMul2 = 0x94D049BB133111EBULL;

std::string toHex(const std::vector<std::uint8_t> &bytes) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (auto b: bytes) {
    oss << std::setw(2) << static_cast<unsigned>(b);
  }
  return oss.str();
}

std::vector<std::uint8_t> fromHex(const std::string &hex) {
  if (hex.size() % 2 != 0) {
    throw std::invalid_argument("Invalid hex payload length");
  }
  std::vector<std::uint8_t> out;
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    auto byte = static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
    out.push_back(byte);
  }
  return out;
}
} // namespace

std::string BloomPartitionMessage::serialize() const {
  std::ostringstream oss;
  oss << partitionIndex << '|'
      << totalPartitions << '|'
      << startBit << '|'
      << originalBitCount << '|'
      << hashFunctionCount << '|'
      << partitionBitCount << '|'
      << toHex(bytes);
  return oss.str();
}

BloomPartitionMessage BloomPartitionMessage::deserialize(const std::string &payload) {
  std::istringstream iss(payload);
  BloomPartitionMessage message;
  std::string token;

  auto nextSizeT = [&](std::size_t &out) {
    if (!std::getline(iss, token, '|')) {
      throw std::invalid_argument("Malformed partition message");
    }
    out = static_cast<std::size_t>(std::stoull(token));
  };

  nextSizeT(message.partitionIndex);
  nextSizeT(message.totalPartitions);
  nextSizeT(message.startBit);
  nextSizeT(message.originalBitCount);
  nextSizeT(message.hashFunctionCount);
  nextSizeT(message.partitionBitCount);
  if (!std::getline(iss, token)) {
    throw std::invalid_argument("Malformed partition message payload");
  }
  message.bytes = fromHex(token);
  return message;
}

BloomFilter::BloomFilter(std::size_t bitCount, std::size_t hashFunctionCount):
    bitCount_(bitCount),
    hashFunctionCount_(hashFunctionCount),
    bits_(bytesForBits(bitCount), 0) {
  if (bitCount_ == 0) {
    throw std::invalid_argument("bitCount must be > 0");
  }
  if (hashFunctionCount_ == 0) {
    throw std::invalid_argument("hashFunctionCount must be > 0");
  }
}

BloomFilter BloomFilter::fromBytes(std::size_t bitCount,
                                   std::size_t hashFunctionCount,
                                   const std::vector<std::uint8_t> &bytes) {
  BloomFilter filter(bitCount, hashFunctionCount);
  if (bytes.size() != filter.bits_.size()) {
    throw std::invalid_argument("Byte length does not match bit count");
  }
  filter.bits_ = bytes;
  return filter;
}

void BloomFilter::insert(std::uint64_t key) {
  for (std::size_t i = 0; i < hashFunctionCount_; ++i) {
    setBit(static_cast<std::size_t>(hashAt(key, i) % bitCount_));
  }
}

bool BloomFilter::contains(std::uint64_t key) const {
  for (std::size_t i = 0; i < hashFunctionCount_; ++i) {
    if (!getBit(static_cast<std::size_t>(hashAt(key, i) % bitCount_))) {
      return false;
    }
  }
  return true;
}

void BloomFilter::mergeOr(const BloomFilter &other) {
  if (bitCount_ != other.bitCount_ || hashFunctionCount_ != other.hashFunctionCount_) {
    throw std::invalid_argument("Cannot merge incompatible BloomFilters");
  }
  for (std::size_t i = 0; i < bits_.size(); ++i) {
    bits_[i] = static_cast<std::uint8_t>(bits_[i] | other.bits_[i]);
  }
}

std::vector<BloomFilterPartition> BloomFilter::split(std::size_t partitions) const {
  if (partitions == 0) {
    throw std::invalid_argument("partitions must be > 0");
  }

  std::vector<BloomFilterPartition> out;
  out.reserve(partitions);

  const std::size_t base = bitCount_ / partitions;
  const std::size_t rem = bitCount_ % partitions;

  std::size_t bitOffset = 0;
  for (std::size_t p = 0; p < partitions; ++p) {
    const std::size_t partBits = base + (p < rem ? 1 : 0);
    BloomFilterPartition part;
    part.partitionIndex = p;
    part.totalPartitions = partitions;
    part.startBit = bitOffset;
    part.originalBitCount = bitCount_;
    part.hashFunctionCount = hashFunctionCount_;
    part.partitionBitCount = partBits;
    part.bytes.assign(bytesForBits(partBits), 0);

    for (std::size_t i = 0; i < partBits; ++i) {
      if (getBit(bitOffset + i)) {
        const std::size_t localBit = i;
        part.bytes[localBit / 8] = static_cast<std::uint8_t>(part.bytes[localBit / 8] | (1u << (localBit % 8)));
      }
    }

    out.push_back(std::move(part));
    bitOffset += partBits;
  }

  return out;
}

BloomFilter BloomFilter::concat(const std::vector<BloomFilterPartition> &partitions) {
  if (partitions.empty()) {
    throw std::invalid_argument("partitions cannot be empty");
  }

  std::vector<BloomFilterPartition> sorted = partitions;
  std::sort(sorted.begin(), sorted.end(), [](const BloomFilterPartition &a, const BloomFilterPartition &b) {
    return a.partitionIndex < b.partitionIndex;
  });

  const auto expectedCount = sorted.front().totalPartitions;
  const auto originalBits = sorted.front().originalBitCount;
  const auto hashCount = sorted.front().hashFunctionCount;

  if (sorted.size() != expectedCount) {
    throw std::invalid_argument("Missing partitions for concat");
  }

  for (std::size_t i = 0; i < sorted.size(); ++i) {
    const auto &part = sorted[i];
    if (part.partitionIndex != i) {
      throw std::invalid_argument("Partition indices must be contiguous");
    }
    if (part.totalPartitions != expectedCount || part.originalBitCount != originalBits ||
        part.hashFunctionCount != hashCount) {
      throw std::invalid_argument("Inconsistent partition metadata");
    }
    if (part.bytes.size() != bytesForBits(part.partitionBitCount)) {
      throw std::invalid_argument("Inconsistent partition byte size");
    }
  }

  BloomFilter out(originalBits, hashCount);
  std::size_t writeBit = 0;
  for (const auto &part: sorted) {
    for (std::size_t localBit = 0; localBit < part.partitionBitCount; ++localBit) {
      const bool isSet = (part.bytes[localBit / 8] & static_cast<std::uint8_t>(1u << (localBit % 8))) != 0;
      if (isSet) {
        out.setBit(writeBit);
      }
      ++writeBit;
    }
  }

  if (writeBit != originalBits) {
    throw std::invalid_argument("Partition bits do not reconstruct original bit count");
  }

  return out;
}

BloomPartitionMessage BloomFilter::toMessage(const BloomFilterPartition &partition) const {
  return BloomPartitionMessage{
      partition.partitionIndex,
      partition.totalPartitions,
      partition.startBit,
      partition.originalBitCount,
      partition.hashFunctionCount,
      partition.partitionBitCount,
      partition.bytes};
}

BloomFilterPartition BloomFilter::partitionFromMessage(const BloomPartitionMessage &message) {
  if (message.bytes.size() != bytesForBits(message.partitionBitCount)) {
    throw std::invalid_argument("Message bytes do not match partition bit count");
  }
  return BloomFilterPartition{
      message.partitionIndex,
      message.totalPartitions,
      message.startBit,
      message.originalBitCount,
      message.hashFunctionCount,
      message.partitionBitCount,
      message.bytes};
}

std::uint64_t BloomFilter::splitMix64(std::uint64_t x) {
  x += kSplitMixIncrement;
  x = (x ^ (x >> 30U)) * kMul1;
  x = (x ^ (x >> 27U)) * kMul2;
  return x ^ (x >> 31U);
}

std::uint64_t BloomFilter::hashAt(std::uint64_t key, std::size_t hashIndex) const {
  return splitMix64(key + static_cast<std::uint64_t>(hashIndex) * kSplitMixIncrement);
}

void BloomFilter::setBit(std::size_t idx) {
  bits_[idx / 8] = static_cast<std::uint8_t>(bits_[idx / 8] | (1u << (idx % 8)));
}

bool BloomFilter::getBit(std::size_t idx) const {
  return (bits_[idx / 8] & static_cast<std::uint8_t>(1u << (idx % 8))) != 0;
}

std::size_t BloomFilter::bytesForBits(std::size_t bitCount) {
  return (bitCount + 7) / 8;
}

} // namespace design3
