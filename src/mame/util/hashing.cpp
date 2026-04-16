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
			sha1_process(m_st, m_buf);
		}
		while ((length - offset) >= 64U)
		{
			for (residual = 0U; residual < 64U; residual++, offset++)
				reinterpret_cast<uint8_t *>(m_buf)[residual ^ swizzle] = reinterpret_cast<const uint8_t *>(data)[offset];
			sha1_process(m_st, m_buf);
		}
		residual = 0U;
	}
	for ( ; offset < length; residual++, offset++)
		reinterpret_cast<uint8_t *>(m_buf)[residual ^ swizzle] = reinterpret_cast<const uint8_t *>(data)[offset];
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
			sha256_transform(m_state, m_buf);
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
		sha256_transform(m_state, m_buf);
		idx = 0;
	}
	memset(m_buf + idx, 0, 56 - idx);

	// append length in bits (big-endian)
	for (int i = 0; i < 8; i++)
		m_buf[56 + i] = uint8_t(bits >> (56 - 8 * i));

	sha256_transform(m_state, m_buf);

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
