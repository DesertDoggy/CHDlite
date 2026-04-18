// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Vas Crabb
/***************************************************************************

    hashing.c

    Hashing helper classes.

***************************************************************************/

#include "hashing.h"

#include "strformat.h"

#include "eminline.h"

#include <zlib.h>

#include <algorithm>
#include <iomanip>
#include <mutex>
#include <sstream>

// SIMD headers for hardware-accelerated SHA1/SHA256
#if defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  if defined(__ARM_FEATURE_CRYPTO) || defined(__ARM_FEATURE_SHA2) || defined(__APPLE__)
#    define CHDLITE_ARM_SHA 1
#  endif
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  if defined(__SHA__) && defined(__SSE4_1__)
#    define CHDLITE_X86_SHA 1
#    include <immintrin.h>
#  elif !defined(_MSC_VER)
     // x86 without compile-time SHA-NI: runtime CPUID dispatch
#    include <cpuid.h>
#    include <immintrin.h>
#    define CHDLITE_X86_SHA_DISPATCH 1
#  elif defined(_MSC_VER)
#    include <intrin.h>
#    include <immintrin.h>
#    define CHDLITE_X86_SHA_DISPATCH 1
#  endif
#endif


namespace util {

//**************************************************************************
//  INLINE FUNCTIONS
//**************************************************************************

namespace {

//-------------------------------------------------
//  char_to_hex - return the hex value of a
//  character
//-------------------------------------------------

constexpr int char_to_hex(char c)
{
	return
			(c >= '0' && c <= '9') ? (c - '0') :
			(c >= 'a' && c <= 'f') ? (10 + c - 'a') :
			(c >= 'A' && c <= 'F') ? (10 + c - 'A') :
			-1;
}


inline uint32_t sha1_b(uint32_t *data, unsigned i) noexcept
{
	uint32_t r = data[(i + 13) & 15U];
	r ^= data[(i + 8) & 15U];
	r ^= data[(i + 2) & 15U];
	r ^= data[i & 15U];
	r = rotl_32(r, 1);
	data[i & 15U] = r;
	return r;
}

inline void sha1_r0(const uint32_t *data, std::array<uint32_t, 5> &d, unsigned i) noexcept
{
	d[i % 5] = d[i % 5] + ((d[(i + 3) % 5] & (d[(i + 2) % 5] ^ d[(i + 1) % 5])) ^ d[(i + 1) % 5]) + data[i] + 0x5a827999U + rotl_32(d[(i + 4) % 5], 5);
	d[(i + 3) % 5] = rotl_32(d[(i + 3) % 5], 30);
}

inline void sha1_r1(uint32_t *data, std::array<uint32_t, 5> &d, unsigned i) noexcept
{
	d[i % 5] = d[i % 5] + ((d[(i + 3) % 5] & (d[(i + 2) % 5] ^ d[(i + 1) % 5])) ^ d[(i + 1) % 5])+ sha1_b(data, i) + 0x5a827999U + rotl_32(d[(i + 4) % 5], 5);
	d[(i + 3) % 5] = rotl_32(d[(i + 3) % 5], 30);
}

inline void sha1_r2(uint32_t *data, std::array<uint32_t, 5> &d, unsigned i) noexcept
{
	d[i % 5] = d[i % 5] + (d[(i + 3) % 5] ^ d[(i + 2) % 5] ^ d[(i + 1) % 5]) + sha1_b(data, i) + 0x6ed9eba1U + rotl_32(d[(i + 4) % 5], 5);
	d[(i + 3) % 5] = rotl_32(d[(i + 3) % 5], 30);
}

inline void sha1_r3(uint32_t *data, std::array<uint32_t, 5> &d, unsigned i) noexcept
{
	d[i % 5] = d[i % 5] + (((d[(i + 3) % 5] | d[(i + 2) % 5]) & d[(i + 1) % 5]) | (d[(i + 3) % 5] & d[(i + 2) % 5])) + sha1_b(data, i) + 0x8f1bbcdcU + rotl_32(d[(i + 4) % 5], 5);
	d[(i + 3) % 5] = rotl_32(d[(i + 3) % 5], 30);
}

inline void sha1_r4(uint32_t *data, std::array<uint32_t, 5> &d, unsigned i) noexcept
{
	d[i % 5] = d[i % 5] + (d[(i + 3) % 5] ^ d[(i + 2) % 5] ^ d[(i + 1) % 5]) + sha1_b(data, i) + 0xca62c1d6U + rotl_32(d[(i + 4) % 5], 5);
	d[(i + 3) % 5] = rotl_32(d[(i + 3) % 5], 30);
}

inline void sha1_process(std::array<uint32_t, 5> &st, uint32_t *data) noexcept
{
	std::array<uint32_t, 5> d = st;
	unsigned i = 0U;
	while (i < 16U)
		sha1_r0(data, d, i++);
	while (i < 20U)
		sha1_r1(data, d, i++);
	while (i < 40U)
		sha1_r2(data, d, i++);
	while (i < 60U)
		sha1_r3(data, d, i++);
	while (i < 80U)
		sha1_r4(data, d, i++);
	for (i = 0U; i < 5U; i++)
		st[i] += d[i];
}

// =====================================================================
// ARM64 Cryptographic Extensions — SHA1
// =====================================================================
#if defined(CHDLITE_ARM_SHA)

inline void sha1_process_arm(std::array<uint32_t, 5> &st, uint32_t *data) noexcept
{
	// MAME state layout: st[4]=A, st[3]=B, st[2]=C, st[1]=D, st[0]=E
	// ARM SHA1 intrinsics expect: ABCD in one uint32x4_t, E scalar
	uint32_t tmp_abcd[4] = { st[4], st[3], st[2], st[1] };
	uint32x4_t abcd = vld1q_u32(tmp_abcd);
	uint32_t   e0   = st[0];

	// Save original state
	uint32x4_t abcd_saved = abcd;
	uint32_t   e0_saved   = e0;

	// Load and byte-swap message (MAME stores data in host-endian in m_buf via XOR swizzle)
	// data[] is already in big-endian uint32_t form (the scalar path reads bytes with ^3 swizzle)
	uint32x4_t msg0 = vld1q_u32(&data[0]);
	uint32x4_t msg1 = vld1q_u32(&data[4]);
	uint32x4_t msg2 = vld1q_u32(&data[8]);
	uint32x4_t msg3 = vld1q_u32(&data[12]);

	uint32x4_t tmp0, tmp1;
	uint32_t e1;

	// Rounds 0-3
	tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x5A827999));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1cq_u32(abcd, e0, tmp0);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	// Rounds 4-7
	tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x5A827999));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1cq_u32(abcd, e1, tmp1);
	msg0 = vsha1su1q_u32(msg0, msg3);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	// Rounds 8-11
	tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x5A827999));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1cq_u32(abcd, e0, tmp0);
	msg1 = vsha1su1q_u32(msg1, msg0);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	// Rounds 12-15
	tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x5A827999));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1cq_u32(abcd, e1, tmp1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	// Rounds 16-19
	tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x5A827999));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1cq_u32(abcd, e0, tmp0);
	msg3 = vsha1su1q_u32(msg3, msg2);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	// Rounds 20-23
	tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x6ED9EBA1));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);
	msg0 = vsha1su1q_u32(msg0, msg3);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	// Rounds 24-27
	tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x6ED9EBA1));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e0, tmp0);
	msg1 = vsha1su1q_u32(msg1, msg0);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	// Rounds 28-31
	tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x6ED9EBA1));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	// Rounds 32-35
	tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x6ED9EBA1));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e0, tmp0);
	msg3 = vsha1su1q_u32(msg3, msg2);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	// Rounds 36-39
	tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x6ED9EBA1));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);
	msg0 = vsha1su1q_u32(msg0, msg3);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	// Rounds 40-43
	tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x8F1BBCDC));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1mq_u32(abcd, e0, tmp0);
	msg1 = vsha1su1q_u32(msg1, msg0);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	// Rounds 44-47
	tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x8F1BBCDC));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1mq_u32(abcd, e1, tmp1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	// Rounds 48-51
	tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x8F1BBCDC));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1mq_u32(abcd, e0, tmp0);
	msg3 = vsha1su1q_u32(msg3, msg2);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	// Rounds 52-55
	tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x8F1BBCDC));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1mq_u32(abcd, e1, tmp1);
	msg0 = vsha1su1q_u32(msg0, msg3);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	// Rounds 56-59
	tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x8F1BBCDC));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1mq_u32(abcd, e0, tmp0);
	msg1 = vsha1su1q_u32(msg1, msg0);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	// Rounds 60-63
	tmp1 = vaddq_u32(msg3, vdupq_n_u32(0xCA62C1D6));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	// Rounds 64-67
	tmp0 = vaddq_u32(msg0, vdupq_n_u32(0xCA62C1D6));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e0, tmp0);
	msg3 = vsha1su1q_u32(msg3, msg2);

	// Rounds 68-71
	tmp1 = vaddq_u32(msg1, vdupq_n_u32(0xCA62C1D6));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);

	// Rounds 72-75
	tmp0 = vaddq_u32(msg2, vdupq_n_u32(0xCA62C1D6));
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e0, tmp0);

	// Rounds 76-79
	tmp1 = vaddq_u32(msg3, vdupq_n_u32(0xCA62C1D6));
	e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
	abcd = vsha1pq_u32(abcd, e1, tmp1);

	// Update state — store back in MAME's reversed order
	st[0] = e0 + e0_saved;
	abcd = vaddq_u32(abcd, abcd_saved);
	uint32_t out_abcd[4];
	vst1q_u32(out_abcd, abcd);
	st[4] = out_abcd[0]; // A
	st[3] = out_abcd[1]; // B
	st[2] = out_abcd[2]; // C
	st[1] = out_abcd[3]; // D
}

#endif // CHDLITE_ARM_SHA

// =====================================================================
// x86 SHA-NI — SHA1 (Intel Ice Lake+ / AMD Zen+)
// =====================================================================
#if defined(CHDLITE_X86_SHA) || defined(CHDLITE_X86_SHA_DISPATCH)

#if defined(CHDLITE_X86_SHA_DISPATCH) && !defined(__SHA__)
// Compile SHA-NI functions with target attribute for runtime dispatch
#if defined(__clang__) || defined(__GNUC__)
#define SHA_TARGET __attribute__((target("sha,sse4.1")))
#elif defined(_MSC_VER)
// MSVC: intrinsics available without target attribute; /arch:AVX2 covers SHA-NI codegen
#define SHA_TARGET
#endif
#else
#define SHA_TARGET
#endif

SHA_TARGET
inline void sha1_process_x86(std::array<uint32_t, 5> &st, uint32_t *data) noexcept
{
	// Based on the canonical SHA-NI reference (noloader/SHA-Intrinsics),
	// adapted for MAME's reversed state layout: st = {E, D, C, B, A}.
	//
	// SHA-NI register conventions:
	//   abcd: lane 3=A, lane 2=B, lane 1=C, lane 0=D
	//   e:    lane 3=E  (other lanes carry message schedule data)
	//
	// Message words must be in lane 3=W[n], lane 0=W[n+3] order,
	// which is the reverse of what _mm_loadu_si128 produces.
	// We use _mm_shuffle_epi32(x, 0x1B) to reverse word order.

	__m128i abcd, e0, e1;
	__m128i msg0, msg1, msg2, msg3;
	__m128i abcd_save, e_save;
	constexpr int REV = 0x1B; // shuffle mask to reverse 4 dwords

	// Load state
	abcd = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&st[1])); // [D, C, B, A]
	e0   = _mm_set_epi32(st[0], 0, 0, 0); // E in lane 3

	abcd_save = abcd;
	e_save    = e0;

	// Load and word-reverse message blocks (data[] is byte-swapped but in
	// low-to-high lane order; SHA-NI needs first word in lane 3)
	msg0 = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[0])),  REV);
	msg1 = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[4])),  REV);
	msg2 = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[8])),  REV);
	msg3 = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[12])), REV);

	// Rounds 0-3
	e0   = _mm_add_epi32(e0, msg0);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);

	// Rounds 4-7
	e1   = _mm_sha1nexte_epu32(e1, msg1);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
	msg0 = _mm_sha1msg1_epu32(msg0, msg1);

	// Rounds 8-11
	e0   = _mm_sha1nexte_epu32(e0, msg2);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
	msg1 = _mm_sha1msg1_epu32(msg1, msg2);
	msg0 = _mm_xor_si128(msg0, msg2);
	msg0 = _mm_sha1msg2_epu32(msg0, msg3);

	// Rounds 12-15
	e1   = _mm_sha1nexte_epu32(e1, msg3);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
	msg2 = _mm_sha1msg1_epu32(msg2, msg3);
	msg1 = _mm_xor_si128(msg1, msg3);
	msg1 = _mm_sha1msg2_epu32(msg1, msg0);

	// Rounds 16-19
	e0   = _mm_sha1nexte_epu32(e0, msg0);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
	msg3 = _mm_sha1msg1_epu32(msg3, msg0);
	msg2 = _mm_xor_si128(msg2, msg0);
	msg2 = _mm_sha1msg2_epu32(msg2, msg1);

	// Rounds 20-23
	e1   = _mm_sha1nexte_epu32(e1, msg1);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
	msg0 = _mm_sha1msg1_epu32(msg0, msg1);
	msg3 = _mm_xor_si128(msg3, msg1);
	msg3 = _mm_sha1msg2_epu32(msg3, msg2);

	// Rounds 24-27
	e0   = _mm_sha1nexte_epu32(e0, msg2);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
	msg1 = _mm_sha1msg1_epu32(msg1, msg2);
	msg0 = _mm_xor_si128(msg0, msg2);
	msg0 = _mm_sha1msg2_epu32(msg0, msg3);

	// Rounds 28-31
	e1   = _mm_sha1nexte_epu32(e1, msg3);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
	msg2 = _mm_sha1msg1_epu32(msg2, msg3);
	msg1 = _mm_xor_si128(msg1, msg3);
	msg1 = _mm_sha1msg2_epu32(msg1, msg0);

	// Rounds 32-35
	e0   = _mm_sha1nexte_epu32(e0, msg0);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
	msg3 = _mm_sha1msg1_epu32(msg3, msg0);
	msg2 = _mm_xor_si128(msg2, msg0);
	msg2 = _mm_sha1msg2_epu32(msg2, msg1);

	// Rounds 36-39
	e1   = _mm_sha1nexte_epu32(e1, msg1);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
	msg0 = _mm_sha1msg1_epu32(msg0, msg1);
	msg3 = _mm_xor_si128(msg3, msg1);
	msg3 = _mm_sha1msg2_epu32(msg3, msg2);

	// Rounds 40-43
	e0   = _mm_sha1nexte_epu32(e0, msg2);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
	msg1 = _mm_sha1msg1_epu32(msg1, msg2);
	msg0 = _mm_xor_si128(msg0, msg2);
	msg0 = _mm_sha1msg2_epu32(msg0, msg3);

	// Rounds 44-47
	e1   = _mm_sha1nexte_epu32(e1, msg3);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
	msg2 = _mm_sha1msg1_epu32(msg2, msg3);
	msg1 = _mm_xor_si128(msg1, msg3);
	msg1 = _mm_sha1msg2_epu32(msg1, msg0);

	// Rounds 48-51
	e0   = _mm_sha1nexte_epu32(e0, msg0);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
	msg3 = _mm_sha1msg1_epu32(msg3, msg0);
	msg2 = _mm_xor_si128(msg2, msg0);
	msg2 = _mm_sha1msg2_epu32(msg2, msg1);

	// Rounds 52-55
	e1   = _mm_sha1nexte_epu32(e1, msg1);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
	msg0 = _mm_sha1msg1_epu32(msg0, msg1);
	msg3 = _mm_xor_si128(msg3, msg1);
	msg3 = _mm_sha1msg2_epu32(msg3, msg2);

	// Rounds 56-59
	e0   = _mm_sha1nexte_epu32(e0, msg2);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
	msg1 = _mm_sha1msg1_epu32(msg1, msg2);
	msg0 = _mm_xor_si128(msg0, msg2);
	msg0 = _mm_sha1msg2_epu32(msg0, msg3);

	// Rounds 60-63
	e1   = _mm_sha1nexte_epu32(e1, msg3);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
	msg2 = _mm_sha1msg1_epu32(msg2, msg3);
	msg1 = _mm_xor_si128(msg1, msg3);
	msg1 = _mm_sha1msg2_epu32(msg1, msg0);

	// Rounds 64-67
	e0   = _mm_sha1nexte_epu32(e0, msg0);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
	msg3 = _mm_sha1msg1_epu32(msg3, msg0);
	msg2 = _mm_xor_si128(msg2, msg0);
	msg2 = _mm_sha1msg2_epu32(msg2, msg1);

	// Rounds 68-71
	e1   = _mm_sha1nexte_epu32(e1, msg1);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
	msg3 = _mm_xor_si128(msg3, msg1);
	msg3 = _mm_sha1msg2_epu32(msg3, msg2);

	// Rounds 72-75
	e0   = _mm_sha1nexte_epu32(e0, msg2);
	e1   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);

	// Rounds 76-79
	e1   = _mm_sha1nexte_epu32(e1, msg3);
	e0   = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

	// Combine with initial state
	e0   = _mm_sha1nexte_epu32(e0, e_save);
	abcd = _mm_add_epi32(abcd, abcd_save);

	// Store back: st = {E, D, C, B, A}
	_mm_storeu_si128(reinterpret_cast<__m128i*>(&st[1]), abcd);
	st[0] = static_cast<uint32_t>(_mm_extract_epi32(e0, 3));
}

// =====================================================================
// x86 SHA-NI — SHA256 transform
// =====================================================================

SHA_TARGET
void sha256_transform_x86(uint32_t state[8], const uint8_t block[64]) noexcept
{
	__m128i state0, state1, state0_save, state1_save;
	__m128i msg, tmp;
	__m128i msg0, msg1, msg2, msg3;
	__m128i shuf_mask = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);

	// Load initial state (ABEF, CDGH order for SHA-NI)
	tmp    = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[0]));  // ABCD
	state1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[4]));  // EFGH

	tmp    = _mm_shuffle_epi32(tmp, 0xB1);       // CDAB
	state1 = _mm_shuffle_epi32(state1, 0x1B);    // HGFE
	state0 = _mm_alignr_epi8(tmp, state1, 8);    // ABEF
	state1 = _mm_blend_epi16(state1, tmp, 0xF0); // CDGH

	state0_save = state0;
	state1_save = state1;

	// Rounds 0-3
	msg0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&block[0]));
	msg0 = _mm_shuffle_epi8(msg0, shuf_mask);
	msg  = _mm_add_epi32(msg0, _mm_set_epi64x(0xE9B5DBA5B5C0FBCFULL, 0x71374491428A2F98ULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

	// Rounds 4-7
	msg1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&block[16]));
	msg1 = _mm_shuffle_epi8(msg1, shuf_mask);
	msg  = _mm_add_epi32(msg1, _mm_set_epi64x(0xAB1C5ED5923F82A4ULL, 0x59F111F13956C25BULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
	msg0 = _mm_sha256msg1_epu32(msg0, msg1);

	// Rounds 8-11
	msg2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&block[32]));
	msg2 = _mm_shuffle_epi8(msg2, shuf_mask);
	msg  = _mm_add_epi32(msg2, _mm_set_epi64x(0x550C7DC3243185BEULL, 0x12835B01D807AA98ULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
	msg1 = _mm_sha256msg1_epu32(msg1, msg2);

	// Rounds 12-15
	msg3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&block[48]));
	msg3 = _mm_shuffle_epi8(msg3, shuf_mask);
	msg  = _mm_add_epi32(msg3, _mm_set_epi64x(0xC19BF174C19BF174ULL, 0x9BDC06A780DEB1FEULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	tmp  = _mm_alignr_epi8(msg3, msg2, 4);
	msg0 = _mm_add_epi32(msg0, tmp);
	msg0 = _mm_sha256msg2_epu32(msg0, msg3);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
	msg2 = _mm_sha256msg1_epu32(msg2, msg3);

	// Rounds 16-19 through 60-63 (repeating pattern)
	#define SHA256_ROUND_X86(ra, rb, rc, rd, k_hi, k_lo) \
		msg  = _mm_add_epi32(ra, _mm_set_epi64x(k_hi, k_lo)); \
		state1 = _mm_sha256rnds2_epu32(state1, state0, msg); \
		tmp  = _mm_alignr_epi8(ra, rd, 4); \
		rb   = _mm_add_epi32(rb, tmp); \
		rb   = _mm_sha256msg2_epu32(rb, ra); \
		msg  = _mm_shuffle_epi32(msg, 0x0E); \
		state0 = _mm_sha256rnds2_epu32(state0, state1, msg); \
		rd   = _mm_sha256msg1_epu32(rd, ra);

	// Rounds 16-19
	SHA256_ROUND_X86(msg0, msg1, msg2, msg3, 0x240CA1CC0FC19DC6ULL, 0xEFBE4786E49B69C1ULL)
	// Rounds 20-23
	SHA256_ROUND_X86(msg1, msg2, msg3, msg0, 0x76F988DA5CB0A9DCULL, 0x4A7484AA2DE92C6FULL)
	// Rounds 24-27
	SHA256_ROUND_X86(msg2, msg3, msg0, msg1, 0xBF597FC7B00327C8ULL, 0xA831C66D983E5152ULL)
	// Rounds 28-31
	SHA256_ROUND_X86(msg3, msg0, msg1, msg2, 0x1429296706CA6351ULL, 0xD5A79147C6E00BF3ULL)
	// Rounds 32-35
	SHA256_ROUND_X86(msg0, msg1, msg2, msg3, 0x53380D134D2C6DFCULL, 0x2E1B213827B70A85ULL)
	// Rounds 36-39
	SHA256_ROUND_X86(msg1, msg2, msg3, msg0, 0x92722C8581C2C92EULL, 0x766A0ABB650A7354ULL)
	// Rounds 40-43
	SHA256_ROUND_X86(msg2, msg3, msg0, msg1, 0xC76C51A3C24B8B70ULL, 0xA81A664BA2BFE8A1ULL)
	// Rounds 44-47
	SHA256_ROUND_X86(msg3, msg0, msg1, msg2, 0x106AA07032BBD1B8ULL, 0xF40E3585D6990624ULL)
	// Rounds 48-51
	SHA256_ROUND_X86(msg0, msg1, msg2, msg3, 0x34B0BCB519A4C116ULL, 0x1E376C082748774CULL)

	#undef SHA256_ROUND_X86

	// Rounds 52-55 (no msg schedule for msg3)
	msg  = _mm_add_epi32(msg1, _mm_set_epi64x(0x682E6FF35B9CCA4FULL, 0x4ED8AA4A391C0CB3ULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	tmp  = _mm_alignr_epi8(msg1, msg0, 4);
	msg2 = _mm_add_epi32(msg2, tmp);
	msg2 = _mm_sha256msg2_epu32(msg2, msg1);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

	// Rounds 56-59
	msg  = _mm_add_epi32(msg2, _mm_set_epi64x(0x8CC7020884C87814ULL, 0x78A5636F748F82EEULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	tmp  = _mm_alignr_epi8(msg2, msg1, 4);
	msg3 = _mm_add_epi32(msg3, tmp);
	msg3 = _mm_sha256msg2_epu32(msg3, msg2);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

	// Rounds 60-63
	msg  = _mm_add_epi32(msg3, _mm_set_epi64x(0xC67178F2BEF9A3F7ULL, 0xA4506CEB90BEFFFAULL));
	state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
	msg  = _mm_shuffle_epi32(msg, 0x0E);
	state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

	// Add saved state
	state0 = _mm_add_epi32(state0, state0_save);
	state1 = _mm_add_epi32(state1, state1_save);

	// Reorder to ABCDEFGH
	tmp    = _mm_shuffle_epi32(state0, 0x1B);    // FEBA
	state1 = _mm_shuffle_epi32(state1, 0xB1);    // DCHG
	state0 = _mm_blend_epi16(tmp, state1, 0xF0); // DCBA → ABCD
	state1 = _mm_alignr_epi8(state1, tmp, 8);    // ABEF → EFGH

	_mm_storeu_si128(reinterpret_cast<__m128i*>(&state[0]), state0);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(&state[4]), state1);
}

#undef SHA_TARGET

// =====================================================================
// x86 CPUID check for SHA-NI support
// =====================================================================
#if defined(CHDLITE_X86_SHA_DISPATCH)
static bool cpu_has_sha_ni() noexcept
{
#if defined(_MSC_VER)
	int info[4];
	__cpuidex(info, 7, 0);
	return (info[1] & (1 << 29)) != 0;  // EBX bit 29 = SHA
#else
	unsigned int eax, ebx, ecx, edx;
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return (ebx & (1 << 29)) != 0;  // EBX bit 29 = SHA
	return false;
#endif
}

static bool s_has_sha_ni = cpu_has_sha_ni();
#endif // CHDLITE_X86_SHA_DISPATCH

#endif // CHDLITE_X86_SHA || CHDLITE_X86_SHA_DISPATCH

// =====================================================================
// ARM64 Cryptographic Extensions — SHA256
// =====================================================================
#if defined(CHDLITE_ARM_SHA)

void sha256_transform_arm(uint32_t state[8], const uint8_t block[64]) noexcept
{
	static const uint32_t sha256_k_arm[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

	uint32x4_t state0 = vld1q_u32(&state[0]);
	uint32x4_t state1 = vld1q_u32(&state[4]);
	uint32x4_t state0_save = state0;
	uint32x4_t state1_save = state1;

	// Load message (big-endian byte-swap)
	uint32x4_t msg0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[0])));
	uint32x4_t msg1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[16])));
	uint32x4_t msg2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[32])));
	uint32x4_t msg3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(&block[48])));

	uint32x4_t tmp0, tmp1;

	#define SHA256_ROUND_ARM(i, m, m0, m1, m2, m3) \
		tmp0  = vaddq_u32(m, vld1q_u32(&sha256_k_arm[(i)*4])); \
		tmp1  = state0; \
		state0 = vsha256hq_u32(state0, state1, tmp0); \
		state1 = vsha256h2q_u32(state1, tmp1, tmp0); \
		if ((i) < 12) m0 = vsha256su1q_u32(vsha256su0q_u32(m0, m1), m2, m3);

	SHA256_ROUND_ARM(0,  msg0, msg0, msg1, msg2, msg3)
	SHA256_ROUND_ARM(1,  msg1, msg1, msg2, msg3, msg0)
	SHA256_ROUND_ARM(2,  msg2, msg2, msg3, msg0, msg1)
	SHA256_ROUND_ARM(3,  msg3, msg3, msg0, msg1, msg2)
	SHA256_ROUND_ARM(4,  msg0, msg0, msg1, msg2, msg3)
	SHA256_ROUND_ARM(5,  msg1, msg1, msg2, msg3, msg0)
	SHA256_ROUND_ARM(6,  msg2, msg2, msg3, msg0, msg1)
	SHA256_ROUND_ARM(7,  msg3, msg3, msg0, msg1, msg2)
	SHA256_ROUND_ARM(8,  msg0, msg0, msg1, msg2, msg3)
	SHA256_ROUND_ARM(9,  msg1, msg1, msg2, msg3, msg0)
	SHA256_ROUND_ARM(10, msg2, msg2, msg3, msg0, msg1)
	SHA256_ROUND_ARM(11, msg3, msg3, msg0, msg1, msg2)
	SHA256_ROUND_ARM(12, msg0, msg0, msg1, msg2, msg3)
	SHA256_ROUND_ARM(13, msg1, msg1, msg2, msg3, msg0)
	SHA256_ROUND_ARM(14, msg2, msg2, msg3, msg0, msg1)
	SHA256_ROUND_ARM(15, msg3, msg3, msg0, msg1, msg2)

	#undef SHA256_ROUND_ARM

	state0 = vaddq_u32(state0, state0_save);
	state1 = vaddq_u32(state1, state1_save);

	vst1q_u32(&state[0], state0);
	vst1q_u32(&state[4], state1);
}

#endif // CHDLITE_ARM_SHA

} // anonymous namespace



//**************************************************************************
//  CONSTANTS
//**************************************************************************

const crc16_t crc16_t::null = { 0 };
const crc32_t crc32_t::null = { 0 };
const md5_t md5_t::null = { { 0 } };
const sha1_t sha1_t::null = { { 0 } };
const sum16_t sum16_t::null = { 0 };



//**************************************************************************
//  SHA-1 HELPERS
//**************************************************************************

//-------------------------------------------------
//  from_string - convert from a string
//-------------------------------------------------

bool sha1_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold everything
	std::fill(std::begin(m_raw), std::end(m_raw), 0);
	if (string.length() < 2 * sizeof(m_raw))
		return false;

	// iterate through our raw buffer
	for (auto &elem : m_raw)
	{
		int const upper = char_to_hex(string[0]);
		int const lower = char_to_hex(string[1]);
		if (upper == -1 || lower == -1)
			return false;
		elem = (upper << 4) | lower;
		string.remove_prefix(2);
	}
	return true;
}


//-------------------------------------------------
//  as_string - convert to a string
//-------------------------------------------------

std::string sha1_t::as_string() const
{
	std::string result(2 * std::size(m_raw), ' ');
	auto it = result.begin();
	for (auto const &elem : m_raw)
	{
		auto const upper = elem >> 4;
		auto const lower = elem & 0x0f;
		*it++ = ((10 > upper) ? '0' : ('a' - 10)) + upper;
		*it++ = ((10 > lower) ? '0' : ('a' - 10)) + lower;
	}
	return result;
}


//-------------------------------------------------
//  reset - prepare to digest a block of data
//-------------------------------------------------

void sha1_creator::reset() noexcept
{
	m_cnt = 0U;
	m_st[0] = 0xc3d2e1f0U;
	m_st[1] = 0x10325476U;
	m_st[2] = 0x98badcfeU;
	m_st[3] = 0xefcdab89U;
	m_st[4] = 0x67452301U;
}


//-------------------------------------------------
//  append - digest a block of data
//-------------------------------------------------

void sha1_creator::append(const void *data, uint32_t length) noexcept
{
// Select the best sha1_process implementation at compile/run time
#if defined(CHDLITE_ARM_SHA)
	#define SHA1_PROCESS(st, buf) sha1_process_arm(st, buf)
#elif defined(CHDLITE_X86_SHA)
	#define SHA1_PROCESS(st, buf) sha1_process_x86(st, buf)
#elif defined(CHDLITE_X86_SHA_DISPATCH)
	#define SHA1_PROCESS(st, buf) do { if (s_has_sha_ni) sha1_process_x86(st, buf); else sha1_process(st, buf); } while(0)
#else
	#define SHA1_PROCESS(st, buf) sha1_process(st, buf)
#endif

#ifdef LSB_FIRST
	constexpr unsigned swizzle = 3U;
#else
	constexpr unsigned swizzle = 0U;
#endif
	uint32_t residual = (uint32_t(m_cnt) >> 3) & 63U;
	m_cnt += uint64_t(length) << 3;
	uint32_t offset = 0U;
	if (length >= (64U - residual))
	{
		if (residual)
		{
			for (offset = 0U; (offset + residual) < 64U; offset++)
				reinterpret_cast<uint8_t *>(m_buf)[(offset + residual) ^ swizzle] = reinterpret_cast<const uint8_t *>(data)[offset];
			SHA1_PROCESS(m_st, m_buf);
		}
		while ((length - offset) >= 64U)
		{
			for (residual = 0U; residual < 64U; residual++, offset++)
				reinterpret_cast<uint8_t *>(m_buf)[residual ^ swizzle] = reinterpret_cast<const uint8_t *>(data)[offset];
			SHA1_PROCESS(m_st, m_buf);
		}
		residual = 0U;
	}
	for ( ; offset < length; residual++, offset++)
		reinterpret_cast<uint8_t *>(m_buf)[residual ^ swizzle] = reinterpret_cast<const uint8_t *>(data)[offset];

#undef SHA1_PROCESS
}


//-------------------------------------------------
//  finish - compute final hash
//-------------------------------------------------

sha1_t sha1_creator::finish() noexcept
{
	const unsigned padlen = 64U - (63U & ((unsigned(m_cnt) >> 3) + 8U));
	uint8_t padbuf[64];
	padbuf[0] = 0x80;
	for (unsigned i = 1U; i < padlen; i++)
		padbuf[i] = 0x00;
	uint8_t lenbuf[8];
	for (unsigned i = 0U; i < 8U; i++)
		lenbuf[i] = uint8_t(m_cnt >> ((7U - i) << 3));
	append(padbuf, padlen);
	append(lenbuf, sizeof(lenbuf));
	sha1_t result;
	for (unsigned i = 0U; i < 20U; i++)
		result.m_raw[i] = uint8_t(m_st[4U - (i >> 2)] >> ((3U - (i & 3)) << 3));
	return result;
}



//**************************************************************************
//  MD-5 HELPERS
//**************************************************************************

//-------------------------------------------------
//  from_string - convert from a string
//-------------------------------------------------

bool md5_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold everything
	std::fill(std::begin(m_raw), std::end(m_raw), 0);
	if (string.length() < 2 * sizeof(m_raw))
		return false;

	// iterate through our raw buffer
	for (auto &elem : m_raw)
	{
		int const upper = char_to_hex(string[0]);
		int const lower = char_to_hex(string[1]);
		if (upper == -1 || lower == -1)
			return false;
		elem = (upper << 4) | lower;
		string.remove_prefix(2);
	}
	return true;
}


//-------------------------------------------------
//  as_string - convert to a string
//-------------------------------------------------

std::string md5_t::as_string() const
{
	std::string result(2 * std::size(m_raw), ' ');
	auto it = result.begin();
	for (auto const &elem : m_raw)
	{
		auto const upper = elem >> 4;
		auto const lower = elem & 0x0f;
		*it++ = ((10 > upper) ? '0' : ('a' - 10)) + upper;
		*it++ = ((10 > lower) ? '0' : ('a' - 10)) + lower;
	}
	return result;
}



//**************************************************************************
//  CRC-32 HELPERS
//**************************************************************************

//-------------------------------------------------
//  from_string - convert from a string
//-------------------------------------------------

bool crc32_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold everything
	m_raw = 0;
	if (string.length() < (2 * sizeof(m_raw)))
		return false;

	// iterate through our raw buffer
	m_raw = 0;
	for (int bytenum = 0; bytenum < sizeof(m_raw) * 2; bytenum++)
	{
		int const nibble = char_to_hex(string[0]);
		if (nibble == -1)
			return false;
		m_raw = (m_raw << 4) | nibble;
		string.remove_prefix(1);
	}
	return true;
}


//-------------------------------------------------
//  as_string - convert to a string
//-------------------------------------------------

std::string crc32_t::as_string() const
{
	return string_format("%08x", m_raw);
}


//-------------------------------------------------
//  append - hash a block of data, appending to
//  the currently-accumulated value
//-------------------------------------------------

void crc32_creator::append(const void *data, uint32_t length) noexcept
{
	m_accum.m_raw = crc32(m_accum, reinterpret_cast<const Bytef *>(data), length);
}



//**************************************************************************
//  CRC-16 HELPERS
//**************************************************************************

//-------------------------------------------------
//  from_string - convert from a string
//-------------------------------------------------

bool crc16_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold everything
	m_raw = 0;
	if (string.length() < (2 * sizeof(m_raw)))
		return false;

	// iterate through our raw buffer
	m_raw = 0;
	for (int bytenum = 0; bytenum < sizeof(m_raw) * 2; bytenum++)
	{
		int const nibble = char_to_hex(string[0]);
		if (nibble == -1)
			return false;
		m_raw = (m_raw << 4) | nibble;
		string.remove_prefix(1);
	}
	return true;
}

/**
 * @fn  std::string crc16_t::as_string() const
 *
 * @brief   -------------------------------------------------
 *            as_string - convert to a string
 *          -------------------------------------------------.
 *
 * @return  a std::string.
 */

std::string crc16_t::as_string() const
{
	return string_format("%04x", m_raw);
}

/**
 * @fn  void crc16_creator::append(const void *data, uint32_t length)
 *
 * @brief   -------------------------------------------------
 *            append - hash a block of data, appending to the currently-accumulated value
 *          -------------------------------------------------.
 *
 * @param   data    The data.
 * @param   length  The length.
 */

void crc16_creator::append(const void *data, uint32_t length) noexcept
{
	static const uint16_t s_table[256] =
	{
		0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
		0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
		0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
		0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
		0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
		0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
		0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
		0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
		0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
		0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
		0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
		0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
		0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
		0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
		0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
		0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
		0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
		0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
		0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
		0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
		0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
		0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
		0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
		0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
		0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
		0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
		0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
		0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
		0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
		0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
		0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
		0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
	};

	const auto *src = reinterpret_cast<const uint8_t *>(data);

	// slice-by-16 precomputed tables (one-time init, thread-safe)
	static uint16_t s_slice[16][256];
	static uint16_t s_slicehi[256];
	static uint16_t s_slicelo[256];
	static uint16_t s_slice16hi[256];
	static uint16_t s_slice16lo[256];
	static std::once_flag s_init;
	std::call_once(s_init, []() {
		auto tc = [](uint16_t v) -> uint16_t {
			return (uint16_t)((v << 8) ^ s_table[v >> 8]);
		};
		for (int b = 0; b < 256; b++) {
			s_slice[0][b] = s_table[b];
			for (int i = 1; i < 16; i++)
				s_slice[i][b] = tc(s_slice[i - 1][b]);
		}
		for (int i = 0; i < 256; i++) {
			uint16_t vh = (uint16_t)(i << 8), vl = (uint16_t)i;
			for (int j = 0; j < 8; j++) { vh = tc(vh); vl = tc(vl); }
			s_slicehi[i] = vh;
			s_slicelo[i] = vl;
			uint16_t vh16 = vh, vl16 = vl;
			for (int j = 0; j < 8; j++) { vh16 = tc(vh16); vl16 = tc(vl16); }
			s_slice16hi[i] = vh16;
			s_slice16lo[i] = vl16;
		}
	});

	// fetch the current value into a local and rip through the source data
	uint16_t crc = m_accum.m_raw;

	// process 16 bytes at a time
	while (length >= 16) {
		crc = (uint16_t)(s_slice16hi[crc >> 8] ^ s_slice16lo[crc & 0xFF]
			^ s_slice[15][src[0]] ^ s_slice[14][src[1]] ^ s_slice[13][src[2]] ^ s_slice[12][src[3]]
			^ s_slice[11][src[4]] ^ s_slice[10][src[5]] ^ s_slice[9][src[6]]  ^ s_slice[8][src[7]]
			^ s_slice[7][src[8]]  ^ s_slice[6][src[9]]  ^ s_slice[5][src[10]] ^ s_slice[4][src[11]]
			^ s_slice[3][src[12]] ^ s_slice[2][src[13]] ^ s_slice[1][src[14]] ^ s_slice[0][src[15]]);
		src += 16;
		length -= 16;
	}
	// process 8 bytes at a time
	while (length >= 8) {
		crc = (uint16_t)(s_slicehi[crc >> 8] ^ s_slicelo[crc & 0xFF]
			^ s_slice[7][src[0]] ^ s_slice[6][src[1]] ^ s_slice[5][src[2]] ^ s_slice[4][src[3]]
			^ s_slice[3][src[4]] ^ s_slice[2][src[5]] ^ s_slice[1][src[6]] ^ s_slice[0][src[7]]);
		src += 8;
		length -= 8;
	}
	// remaining bytes
	while (length--)
		crc = (uint16_t)((crc << 8) ^ s_table[(crc >> 8) ^ *src++]);

	m_accum.m_raw = crc;
}



//**************************************************************************
//  SUM-16 HELPERS
//**************************************************************************

//-------------------------------------------------
//  from_string - convert from a string
//-------------------------------------------------

bool sum16_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold everything
	m_raw = 0;
	if (string.length() < (2 * sizeof(m_raw)))
		return false;

	// iterate through our raw buffer
	m_raw = 0;
	for (int bytenum = 0; bytenum < sizeof(m_raw) * 2; bytenum++)
	{
		int const nibble = char_to_hex(string[0]);
		if (nibble == -1)
			return false;
		m_raw = (m_raw << 4) | nibble;
		string.remove_prefix(1);
	}
	return true;
}

/**
 * @fn  std::string sum16_t::as_string() const
 *
 * @brief   -------------------------------------------------
 *            as_string - convert to a string
 *          -------------------------------------------------.
 *
 * @return  a std::string.
 */

std::string sum16_t::as_string() const
{
	return string_format("%04x", m_raw);
}

/**
 * @fn  void sum16_creator::append(const void *data, uint32_t length)
 *
 * @brief   -------------------------------------------------
 *            append - sum a block of data, appending to the currently-accumulated value
 *          -------------------------------------------------.
 *
 * @param   data    The data.
 * @param   length  The length.
 */

void sum16_creator::append(const void *data, uint32_t length) noexcept
{
	const auto *src = reinterpret_cast<const uint8_t *>(data);

	// fetch the current value into a local and rip through the source data
	uint16_t sum = m_accum.m_raw;
	while (length-- != 0)
		sum += *src++;
	m_accum.m_raw = sum;
}


//**************************************************************************
//  SHA-256
//**************************************************************************

const sha256_t sha256_t::null = { { 0 } };

bool sha256_t::from_string(std::string_view string) noexcept
{
	// must be at least long enough to hold the data
	memset(m_raw, 0, sizeof(m_raw));
	if (string.length() < 2 * sizeof(m_raw))
		return false;

	// iterate through our raw buffer
	for (auto & elem : m_raw)
	{
		int upper = char_to_hex(string[0]);
		int lower = char_to_hex(string[1]);
		if (upper == -1 || lower == -1)
			return false;
		elem = (upper << 4) | lower;
		string.remove_prefix(2);
	}
	return true;
}

std::string sha256_t::as_string() const
{
	std::ostringstream buffer;
	buffer << std::hex << std::setfill('0');
	for (auto & elem : m_raw)
		buffer << std::setw(2) << unsigned(elem);
	return buffer.str();
}

namespace {

static constexpr uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t sha256_rotr(uint32_t x, unsigned n) noexcept { return (x >> n) | (x << (32 - n)); }
inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (~x & z); }
inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sha256_ep0(uint32_t x) noexcept { return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22); }
inline uint32_t sha256_ep1(uint32_t x) noexcept { return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25); }
inline uint32_t sha256_sig0(uint32_t x) noexcept { return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3); }
inline uint32_t sha256_sig1(uint32_t x) noexcept { return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10); }

void sha256_transform(uint32_t state[8], const uint8_t block[64]) noexcept
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
		       (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
	for (int i = 16; i < 64; i++)
		w[i] = sha256_sig1(w[i-2]) + w[i-7] + sha256_sig0(w[i-15]) + w[i-16];

	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

	for (int i = 0; i < 64; i++)
	{
		uint32_t t1 = h + sha256_ep1(e) + sha256_ch(e, f, g) + sha256_k[i] + w[i];
		uint32_t t2 = sha256_ep0(a) + sha256_maj(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // anonymous namespace

// SHA256 transform dispatch
static inline void sha256_transform_dispatch(uint32_t state[8], const uint8_t block[64]) noexcept
{
#if defined(CHDLITE_ARM_SHA)
	sha256_transform_arm(state, block);
#elif defined(CHDLITE_X86_SHA)
	sha256_transform_x86(state, block);
#elif defined(CHDLITE_X86_SHA_DISPATCH)
	if (s_has_sha_ni)
		sha256_transform_x86(state, block);
	else
		sha256_transform(state, block);
#else
	sha256_transform(state, block);
#endif
}

void sha256_creator::reset() noexcept
{
	m_cnt = 0;
	m_state[0] = 0x6a09e667; m_state[1] = 0xbb67ae85;
	m_state[2] = 0x3c6ef372; m_state[3] = 0xa54ff53a;
	m_state[4] = 0x510e527f; m_state[5] = 0x9b05688c;
	m_state[6] = 0x1f83d9ab; m_state[7] = 0x5be0cd19;
}

void sha256_creator::append(const void *data, uint32_t length) noexcept
{
	const auto *src = reinterpret_cast<const uint8_t *>(data);
	uint32_t idx = static_cast<uint32_t>(m_cnt & 63);
	m_cnt += length;

	while (length > 0)
	{
		uint32_t tocopy = std::min(length, 64u - idx);
		memcpy(m_buf + idx, src, tocopy);
		idx += tocopy;
		src += tocopy;
		length -= tocopy;

		if (idx == 64)
		{
			sha256_transform_dispatch(m_state, m_buf);
			idx = 0;
		}
	}
}

sha256_t sha256_creator::finish() noexcept
{
	uint64_t bits = m_cnt * 8;
	uint32_t idx = static_cast<uint32_t>(m_cnt & 63);

	// pad
	m_buf[idx++] = 0x80;
	if (idx > 56)
	{
		memset(m_buf + idx, 0, 64 - idx);
		sha256_transform_dispatch(m_state, m_buf);
		idx = 0;
	}
	memset(m_buf + idx, 0, 56 - idx);

	// append length in bits (big-endian)
	for (int i = 0; i < 8; i++)
		m_buf[56 + i] = uint8_t(bits >> (56 - 8 * i));

	sha256_transform_dispatch(m_state, m_buf);

	sha256_t result;
	for (int i = 0; i < 8; i++)
	{
		result.m_raw[i*4+0] = uint8_t(m_state[i] >> 24);
		result.m_raw[i*4+1] = uint8_t(m_state[i] >> 16);
		result.m_raw[i*4+2] = uint8_t(m_state[i] >> 8);
		result.m_raw[i*4+3] = uint8_t(m_state[i]);
	}
	return result;
}

} // namespace util
