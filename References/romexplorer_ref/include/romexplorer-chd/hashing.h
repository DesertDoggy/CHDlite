#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "models.h"

namespace romexplorer {

/**
 * Result container for a single hash value
 */
struct HashResult {
    std::string hash_hex;        // Hash value as hex string
    std::time_t hash_date = 0;   // When was this hash calculated?
};

/**
 * Result container for all supported hash algorithms
 * Used when computing multiple hashes simultaneously
 */
struct HashesResults {
    std::string xxhash3_128_hex;
    std::time_t xxhash3_128_date = 0;
    std::string sha1_hex;
    std::time_t sha1_date = 0;
    std::string md5_hex;
    std::time_t md5_date = 0;
    std::string crc32_hex;
    std::time_t crc32_date = 0;
    std::string sha256_hex;
    std::time_t sha256_date = 0;
    std::string sha384_hex;
    std::time_t sha384_date = 0;
    std::string sha512_hex;
    std::time_t sha512_date = 0;
};

/**
 * Get list of supported hash algorithm names
 */
std::vector<std::string> get_supported_algorithms();

/**
 * Calculate hashes for a file
 * @param file_path Path to file
 * @param hash_algorithms Algorithms to compute (default: CRC32, MD5, SHA1, SHA256, XXHASH3_128)
 * @param chunk_size Read size per iteration (default: 64MB)
 * @return Container with computed hashes
 */
HashesResults calculate_hashes(
    const std::string& file_path,
    const std::vector<HashAlgorithm>& hash_algorithms = {
        HashAlgorithm::CRC32, HashAlgorithm::MD5, HashAlgorithm::SHA1,
        HashAlgorithm::SHA256, HashAlgorithm::XXHASH3_128
    },
    uint64_t chunk_size = 67108864);  // 64MB default

/**
 * Extract a single hash from a HashesResults container
 */
HashResult get_hash_from_HashesResults(const HashesResults& hashes_result, HashAlgorithm algorithm);

/**
 * Get the timestamp when hashing was completed
 */
std::time_t get_date(const HashesResults& results);

/**
 * Calculate hashes for an in-memory buffer
 * @param data Pointer to buffer
 * @param size Size in bytes
 * @param hash_algorithms Algorithms to compute (default: CRC32, MD5, SHA1, SHA256, XXHASH3_128)
 * @return Container with computed hashes
 */
HashesResults calculate_hashes_from_memory(
    const void* data, std::size_t size,
    const std::vector<HashAlgorithm>& hash_algorithms = {
        HashAlgorithm::CRC32, HashAlgorithm::MD5, HashAlgorithm::SHA1,
        HashAlgorithm::SHA256, HashAlgorithm::XXHASH3_128
    });

}  // namespace romexplorer
