#include "hashing.h"
#include "models.h"
#include "logger.h"

#include <string>
#include <vector>
#include <cstdint>

#include <magic_enum/magic_enum.hpp>
#include <zlib.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <xxhash.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <stdexcept>

namespace romexplorer {

std::vector<std::string> get_supported_algorithms() {
    std::vector<std::string> result;
    for (const auto& entry : magic_enum::enum_entries<HashAlgorithm>()) {
        result.emplace_back(entry.second);
    }
    return result;
}

HashesResults calculate_hashes(const std::string& file_path,
                              const std::vector<HashAlgorithm>& hash_algorithms,
                              uint64_t chunk_size) {
    std::stringstream algo_str;
    for (size_t i = 0; i < hash_algorithms.size(); ++i) {
        if (i > 0) algo_str << ", ";
        algo_str << magic_enum::enum_name(hash_algorithms[i]);
    }
    LOG_DEBUG_STREAM("[HASH] Starting hash calculation: " << file_path);
    LOG_DEBUG_STREAM("[HASH] Algorithms: " << algo_str.str());
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + file_path);
    }

    file.seekg(0, std::ios::end);
    uint64_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    LOG_DEBUG_STREAM("[HASH] File size: " << file_size << " bytes");

    std::vector<std::string_view> algo_names;
    for (const auto& algo : hash_algorithms) {
        algo_names.emplace_back(magic_enum::enum_name(algo));
    }
    HashesResults results;

    XXH3_state_t* state = XXH3_createState();
    XXH3_128bits_reset(state);
    
    EVP_MD_CTX *sha1_ctx = EVP_MD_CTX_new();
    const EVP_MD *sha1_md = EVP_sha1();
    EVP_DigestInit_ex(sha1_ctx, sha1_md, nullptr);

    EVP_MD_CTX *md5_ctx = EVP_MD_CTX_new();
    const EVP_MD *md5_md = EVP_md5();
    EVP_DigestInit_ex(md5_ctx, md5_md, nullptr);

    EVP_MD_CTX *sha256_ctx = EVP_MD_CTX_new();
    const EVP_MD *sha256_md = EVP_sha256();
    EVP_DigestInit_ex(sha256_ctx, sha256_md, nullptr);

    EVP_MD_CTX *sha384_ctx = EVP_MD_CTX_new();
    const EVP_MD *sha384_md = EVP_sha384();
    EVP_DigestInit_ex(sha384_ctx, sha384_md, nullptr);

    EVP_MD_CTX *sha512_ctx = EVP_MD_CTX_new();
    const EVP_MD *sha512_md = EVP_sha512();
    EVP_DigestInit_ex(sha512_ctx, sha512_md, nullptr);

    unsigned long crc32_temp = crc32(0L, Z_NULL, 0);

    bool has_xxhash3_128 = std::find(algo_names.begin(), algo_names.end(), "XXHASH3_128") != algo_names.end();
    bool has_sha1 = std::find(algo_names.begin(), algo_names.end(), "SHA1") != algo_names.end();
    bool has_md5 = std::find(algo_names.begin(), algo_names.end(), "MD5") != algo_names.end();
    bool has_sha256 = std::find(algo_names.begin(), algo_names.end(), "SHA256") != algo_names.end();
    bool has_sha384 = std::find(algo_names.begin(), algo_names.end(), "SHA384") != algo_names.end();
    bool has_sha512 = std::find(algo_names.begin(), algo_names.end(), "SHA512") != algo_names.end();
    bool has_crc32 = std::find(algo_names.begin(), algo_names.end(), "CRC32") != algo_names.end();

    std::vector<char> buffer(chunk_size);

    while (file.read(buffer.data(), chunk_size) || file.gcount() > 0) {
        uint64_t bytes_read = file.gcount();

        if (has_xxhash3_128) {
            XXH3_128bits_update(state, buffer.data(), bytes_read);
        }
        if (has_sha1) {
            EVP_DigestUpdate(sha1_ctx, buffer.data(), bytes_read);
        }
        if (has_md5) {
            EVP_DigestUpdate(md5_ctx, buffer.data(), bytes_read);
        }
        if (has_sha256) {
            EVP_DigestUpdate(sha256_ctx, buffer.data(), bytes_read);
        }
        if (has_sha384) {
            EVP_DigestUpdate(sha384_ctx, buffer.data(), bytes_read);
        }
        if (has_sha512) {
            EVP_DigestUpdate(sha512_ctx, buffer.data(), bytes_read);
        }
        if (has_crc32) {
            crc32_temp = crc32(crc32_temp, reinterpret_cast<const unsigned char*>(buffer.data()), bytes_read);
        }
    }

    auto bytes_to_hex = [](const unsigned char* bytes, size_t len) {
        std::stringstream ss;
        for (size_t i = 0; i < len; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
        }
        return ss.str();
    };

    std::time_t finish_time = std::time(nullptr);

    if (has_sha1) {
        unsigned char sha1_digest[SHA_DIGEST_LENGTH];
        unsigned int sha1_len = 0;
        EVP_DigestFinal_ex(sha1_ctx, sha1_digest, &sha1_len);
        results.sha1_hex = bytes_to_hex(sha1_digest, sha1_len);
        results.sha1_date = finish_time;
        EVP_MD_CTX_free(sha1_ctx);
    }

    if (has_md5) {
        unsigned char md5_digest[MD5_DIGEST_LENGTH];
        unsigned int md5_len = 0;
        EVP_DigestFinal_ex(md5_ctx, md5_digest, &md5_len);
        results.md5_hex = bytes_to_hex(md5_digest, md5_len);
        results.md5_date = finish_time;
        EVP_MD_CTX_free(md5_ctx);
    }

    if (has_sha256) {
        unsigned char sha256_digest[SHA256_DIGEST_LENGTH];
        unsigned int sha256_len = 0;
        EVP_DigestFinal_ex(sha256_ctx, sha256_digest, &sha256_len);
        results.sha256_hex = bytes_to_hex(sha256_digest, sha256_len);
        results.sha256_date = finish_time;
        EVP_MD_CTX_free(sha256_ctx);
    }

    if (has_sha384) {
        unsigned char sha384_digest[SHA384_DIGEST_LENGTH];
        unsigned int sha384_len = 0;
        EVP_DigestFinal_ex(sha384_ctx, sha384_digest, &sha384_len);
        results.sha384_hex = bytes_to_hex(sha384_digest, sha384_len);
        results.sha384_date = finish_time;
        EVP_MD_CTX_free(sha384_ctx);
    }

    if (has_sha512) {
        unsigned char sha512_digest[SHA512_DIGEST_LENGTH];
        unsigned int sha512_len = 0;
        EVP_DigestFinal_ex(sha512_ctx, sha512_digest, &sha512_len);
        results.sha512_hex = bytes_to_hex(sha512_digest, sha512_len);
        results.sha512_date = finish_time;
        EVP_MD_CTX_free(sha512_ctx);
    }

    if (has_crc32) {
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << crc32_temp;
        results.crc32_hex = ss.str();
        results.crc32_date = finish_time;
    }

    if (has_xxhash3_128) {
        XXH128_hash_t hash = XXH3_128bits_digest(state);
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(16) << hash.high64
           << std::setw(16) << hash.low64;
        results.xxhash3_128_hex = ss.str();
        results.xxhash3_128_date = finish_time;
        XXH3_freeState(state);
    }

    if (!results.crc32_hex.empty()) LOG_DEBUG_STREAM("[HASH] CRC32:      " << results.crc32_hex);
    if (!results.md5_hex.empty())   LOG_DEBUG_STREAM("[HASH] MD5:        " << results.md5_hex);
    if (!results.sha1_hex.empty())  LOG_DEBUG_STREAM("[HASH] SHA1:       " << results.sha1_hex);
    if (!results.sha256_hex.empty()) LOG_DEBUG_STREAM("[HASH] SHA256:     " << results.sha256_hex);
    if (!results.sha384_hex.empty()) LOG_DEBUG_STREAM("[HASH] SHA384:     " << results.sha384_hex);
    if (!results.sha512_hex.empty()) LOG_DEBUG_STREAM("[HASH] SHA512:     " << results.sha512_hex);
    if (!results.xxhash3_128_hex.empty()) LOG_DEBUG_STREAM("[HASH] XXHASH3:    " << results.xxhash3_128_hex);
    LOG_DEBUG("[HASH] Hash calculation completed");
    
    return results;
}

HashesResults calculate_hashes_from_memory(const void* data, std::size_t size,
                                            const std::vector<HashAlgorithm>& hash_algorithms)
{
    HashesResults results;

    std::vector<std::string_view> algo_names;
    for (const auto& algo : hash_algorithms)
        algo_names.emplace_back(magic_enum::enum_name(algo));

    bool has_xxhash3_128 = std::find(algo_names.begin(), algo_names.end(), "XXHASH3_128") != algo_names.end();
    bool has_sha1   = std::find(algo_names.begin(), algo_names.end(), "SHA1")   != algo_names.end();
    bool has_md5    = std::find(algo_names.begin(), algo_names.end(), "MD5")    != algo_names.end();
    bool has_sha256 = std::find(algo_names.begin(), algo_names.end(), "SHA256") != algo_names.end();
    bool has_sha384 = std::find(algo_names.begin(), algo_names.end(), "SHA384") != algo_names.end();
    bool has_sha512 = std::find(algo_names.begin(), algo_names.end(), "SHA512") != algo_names.end();
    bool has_crc32  = std::find(algo_names.begin(), algo_names.end(), "CRC32")  != algo_names.end();

    XXH3_state_t* xxh_state = has_xxhash3_128 ? XXH3_createState() : nullptr;
    if (xxh_state) XXH3_128bits_reset(xxh_state);

    EVP_MD_CTX* sha1_ctx   = has_sha1   ? EVP_MD_CTX_new() : nullptr;
    EVP_MD_CTX* md5_ctx    = has_md5    ? EVP_MD_CTX_new() : nullptr;
    EVP_MD_CTX* sha256_ctx = has_sha256 ? EVP_MD_CTX_new() : nullptr;
    EVP_MD_CTX* sha384_ctx = has_sha384 ? EVP_MD_CTX_new() : nullptr;
    EVP_MD_CTX* sha512_ctx = has_sha512 ? EVP_MD_CTX_new() : nullptr;

    if (sha1_ctx)   EVP_DigestInit_ex(sha1_ctx,   EVP_sha1(),   nullptr);
    if (md5_ctx)    EVP_DigestInit_ex(md5_ctx,    EVP_md5(),    nullptr);
    if (sha256_ctx) EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), nullptr);
    if (sha384_ctx) EVP_DigestInit_ex(sha384_ctx, EVP_sha384(), nullptr);
    if (sha512_ctx) EVP_DigestInit_ex(sha512_ctx, EVP_sha512(), nullptr);

    unsigned long crc32_val = crc32(0L, Z_NULL, 0);

    if (size > 0) {
        if (xxh_state) XXH3_128bits_update(xxh_state, data, size);
        if (sha1_ctx)   EVP_DigestUpdate(sha1_ctx,   data, size);
        if (md5_ctx)    EVP_DigestUpdate(md5_ctx,    data, size);
        if (sha256_ctx) EVP_DigestUpdate(sha256_ctx, data, size);
        if (sha384_ctx) EVP_DigestUpdate(sha384_ctx, data, size);
        if (sha512_ctx) EVP_DigestUpdate(sha512_ctx, data, size);
        if (has_crc32)  crc32_val = crc32(crc32_val,
                                          static_cast<const Bytef*>(data),
                                          static_cast<uInt>(size));
    }

    auto bytes_to_hex = [](const unsigned char* bytes, size_t len) {
        std::stringstream ss;
        for (size_t i = 0; i < len; ++i)
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
        return ss.str();
    };

    std::time_t finish_time = std::time(nullptr);

    if (sha1_ctx) {
        unsigned char d[SHA_DIGEST_LENGTH]; unsigned int l = 0;
        EVP_DigestFinal_ex(sha1_ctx, d, &l);
        results.sha1_hex = bytes_to_hex(d, l);
        results.sha1_date = finish_time;
        EVP_MD_CTX_free(sha1_ctx);
    }
    if (md5_ctx) {
        unsigned char d[MD5_DIGEST_LENGTH]; unsigned int l = 0;
        EVP_DigestFinal_ex(md5_ctx, d, &l);
        results.md5_hex = bytes_to_hex(d, l);
        results.md5_date = finish_time;
        EVP_MD_CTX_free(md5_ctx);
    }
    if (sha256_ctx) {
        unsigned char d[SHA256_DIGEST_LENGTH]; unsigned int l = 0;
        EVP_DigestFinal_ex(sha256_ctx, d, &l);
        results.sha256_hex = bytes_to_hex(d, l);
        results.sha256_date = finish_time;
        EVP_MD_CTX_free(sha256_ctx);
    }
    if (sha384_ctx) {
        unsigned char d[SHA384_DIGEST_LENGTH]; unsigned int l = 0;
        EVP_DigestFinal_ex(sha384_ctx, d, &l);
        results.sha384_hex = bytes_to_hex(d, l);
        results.sha384_date = finish_time;
        EVP_MD_CTX_free(sha384_ctx);
    }
    if (sha512_ctx) {
        unsigned char d[SHA512_DIGEST_LENGTH]; unsigned int l = 0;
        EVP_DigestFinal_ex(sha512_ctx, d, &l);
        results.sha512_hex = bytes_to_hex(d, l);
        results.sha512_date = finish_time;
        EVP_MD_CTX_free(sha512_ctx);
    }
    if (has_crc32) {
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << crc32_val;
        results.crc32_hex = ss.str();
        results.crc32_date = finish_time;
    }
    if (xxh_state) {
        XXH128_hash_t h = XXH3_128bits_digest(xxh_state);
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(16) << h.high64
           << std::setw(16) << h.low64;
        results.xxhash3_128_hex = ss.str();
        results.xxhash3_128_date = finish_time;
        XXH3_freeState(xxh_state);
    }

    return results;
}

HashResult get_hash_from_HashesResults(const HashesResults& hashes_results, HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::XXHASH3_128:
            return {hashes_results.xxhash3_128_hex, hashes_results.xxhash3_128_date};
        case HashAlgorithm::SHA1:
            return {hashes_results.sha1_hex, hashes_results.sha1_date};
        case HashAlgorithm::MD5:
            return {hashes_results.md5_hex, hashes_results.md5_date};
        case HashAlgorithm::CRC32:
            return {hashes_results.crc32_hex, hashes_results.crc32_date};
        case HashAlgorithm::SHA256:
            return {hashes_results.sha256_hex, hashes_results.sha256_date};
        case HashAlgorithm::SHA384:
            return {hashes_results.sha384_hex, hashes_results.sha384_date};
        case HashAlgorithm::SHA512:
            return {hashes_results.sha512_hex, hashes_results.sha512_date};
        default:
            throw std::invalid_argument("Unsupported hash algorithm");
    }
}

std::time_t get_date(const HashesResults& results) {
    if (results.crc32_date != 0) return results.crc32_date;
    if (results.md5_date != 0) return results.md5_date;
    if (results.sha1_date != 0) return results.sha1_date;
    if (results.sha256_date != 0) return results.sha256_date;
    if (results.sha384_date != 0) return results.sha384_date;
    if (results.sha512_date != 0) return results.sha512_date;
    if (results.xxhash3_128_date != 0) return results.xxhash3_128_date;
    return 0;
}

}  // namespace romexplorer
