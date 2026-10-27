#pragma once
#include <fstream>
#include <filesystem>
#include <bit>
#include <optional>
#include <string>
#include <chrono>
#include "SpeechServiceConstants.h"
#include <openssl/sha.h>

namespace Drm {

constexpr inline char EMBEDDED_V1_ENCRYPT_KEY[] = {
	0x23, 0x23, 0x45, 0x6e, 0x63, 0x72, 0x79, 0x70, 0x74, 0x65, 0x64, 0x23, 0x23
};
constexpr inline char GEC_V2_PREFIX[] = {
	0x43, 0x6f, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x28, 0x43, 0x29, 0x20, 0x4d,
	0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x20, 0x43, 0x6f, 0x72, 0x70, 0x6f, 0x72,
	0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2e, 0x20, 0x41, 0x6c, 0x6c, 0x20, 0x72, 0x69, 0x67, 0x68,
	0x74, 0x73, 0x20, 0x72, 0x65, 0x73, 0x65, 0x72, 0x76, 0x65, 0x64, 0x2e
};

inline std::optional<std::string> EmbeddedV0Key(std::istream& model_file)
{
	if (!model_file.seekg(0, std::ios::end))
		return {};
	const size_t size = model_file.tellg();
	model_file.seekg(0);

	// key length = first 4-byte little-endian integer
	uint32_t keylen;
	if (!model_file.read(reinterpret_cast<char*>(&keylen), sizeof keylen))
		return {};
	if constexpr (std::endian::native != std::endian::little)
		keylen = std::byteswap(keylen);
	if (keylen > size - sizeof keylen || keylen > 64 << 10)  // capped at 64KB max
		return {};

	// XOR obfuscated key = <keylen> bytes afterwards
	std::string key(keylen, '\0');
	if (!model_file.read(key.data(), keylen))
		return {};

	size_t i = 0;
	for (char& ch: key)
	{
		ch ^= EMBEDDED_V1_ENCRYPT_KEY[i];
		i++;
		if (i >= sizeof EMBEDDED_V1_ENCRYPT_KEY)
			i = 0;
	}

	return std::move(key);
}

inline std::optional<std::string> EmbeddedV1Lic(std::istream& model_file)
{
	// assume that the license is before the first 16 hexadecimal digits
	std::string key;
	size_t hexchar_count = 0;
	model_file.seekg(0);
	for (;;)
	{
		int ch = model_file.get();
		if (!std::isprint(ch))
			return {};
		key.push_back(static_cast<char>(ch));
		if (ch >= 'a' && ch <= 'f' || ch >= '0' && ch <= '9')
			hexchar_count++;
		else
			hexchar_count = 0;
		if (hexchar_count == 16)
		{
			key.erase(key.length() - 16);
			return std::move(key);
		}
	}
}

inline std::optional<std::string> GetEmbeddedLicForModel(const std::filesystem::path& model_dir_path)
{
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(model_dir_path, ec))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".bin")
		{
			std::ifstream model_file(entry.path(), std::ios::in | std::ios::binary);
			if (auto key = EmbeddedV1Lic(model_file))
			{
				return key;
			}
			model_file.clear();
			if (auto key = EmbeddedV0Key(model_file))
			{
				key->insert(0, "Key:");
				return key;
			}
		}
	}
	return {};
}

inline std::string GecTimestampedToken(std::chrono::system_clock::duration offset)
{
	// Unix epoch = 1970-01-01 00:00:00 UTC
	// FILETIME epoch = 1601-01-01 00:00:00 UTC
	const auto unix_now = std::chrono::system_clock::now().time_since_epoch() + offset;  // add the time delta to the local time
	const auto filetime_now = unix_now + std::chrono::seconds(11'644'473'600);
	const auto rounded_now = filetime_now - filetime_now % std::chrono::minutes(5);  // round down to the nearest 5 minute

	using TickDuration = std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>;
	const auto ticks = std::chrono::duration_cast<TickDuration>(rounded_now).count();

	// concatenate ticks & trusted client token into a string
	return std::to_string(ticks) + EDGE_TRUSTED_CLIENT_TOKEN;
}

inline std::string HexString(const unsigned char* data, size_t len)
{
	std::string result(len * 2, '\0');
	char* pCh = result.data();
	constexpr char HexDigits[] = "0123456789ABCDEF";
	auto* const pEnd = data + len;
	for (auto* pByte = data; pByte != pEnd; pByte++)
	{
		auto byte = *pByte;
		*pCh++ = HexDigits[(byte & 0xF0) >> 4];
		*pCh++ = HexDigits[byte & 0x0F];
	}
	return result;
}

inline std::string GecV1Token(std::chrono::system_clock::duration offset)
{
	// algorithm for generating the Sec-MS-GEC token
	// see: https://github.com/rany2/edge-tts/issues/290#issuecomment-2464956570
	const auto str = GecTimestampedToken(offset);

	// calculate SHA-256 hash
	unsigned char hash[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const unsigned char*>(str.data()), str.size(), hash))
		throw std::runtime_error("GEC token generation failed");

	return HexString(hash, SHA256_DIGEST_LENGTH);
}

constexpr uint32_t WordSwap(uint32_t x) noexcept { return (x << 16) | (x >> 16); }

// Modified version of Chromium ComputeHash() for Edge Sec-MS-GEC v2 algorithm.
// Source:
//    https://chromium.googlesource.com/chromium/src/%2B/4a8573cb240df29b0e4d9820303538fb28e31d84/chrome/installer/util/shell_util.cc
// The source code above is governed by a BSD-style license:
//    https://chromium.googlesource.com/chromium/src/%2B/4a8573cb240df29b0e4d9820303538fb28e31d84/LICENSE
// Return an 8-byte hash.
inline std::array<uint32_t, 2> ComputeHash(std::string input) {
	// Assumes little-endian currently
	static_assert(std::endian::native == std::endian::little);

	// Zero-pad incomplete 8-byte blocks.
	const size_t items_block_aligned = ((input.size() + 7) / 8) * 2;
	std::vector<uint32_t> input_32(items_block_aligned);
	if (!input.empty()) {
		memcpy(input_32.data(), input.data(), input.size());
	}

	unsigned char sha256_digest[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), sha256_digest))
		throw std::runtime_error("ComputeHash failed");
	uint32_t sha256[2];
	memcpy(sha256, sha256_digest, sizeof(sha256));

	std::array<uint32_t, 4> result{};
	const uint32_t key_0 = sha256[0] | 1;
	const uint32_t key_1 = sha256[1] | 1;
	const size_t length = input_32.size();

	uint32_t part_1 = 0;
	uint32_t part_2 = 0;
	for (size_t pos = 0; pos < length; ++pos) {
		if (pos & 1) {
			// Independent of previous part_1.
			part_1 = input_32[pos] * key_1 + WordSwap(input_32[pos]) * 0xC31713DB;
			part_1 = WordSwap(part_1) * 0x1881CE3C;
			result[0] = part_1 + WordSwap(part_1) * 0x35BD1EC9;
			result[1] += result[0];

			const uint32_t prev_part_2 = part_2;
			part_2 = input_32[pos] + prev_part_2;
			part_2 = key_1 * part_2;
			part_2 = 0x16F50000 * part_2 + 0xA27416F5 * (part_2 >> 16);
			part_2 = 0x96FF0000 * part_2 + 0xD38396FF * (part_2 >> 16);
			part_2 = 0x2B890000 * part_2 + 0x7C932B89 * (part_2 >> 16);
			result[2] = 0x9F690000 * part_2 + 0xBFA49F69 * (part_2 >> 16);
			result[3] = result[2] + prev_part_2 + result[3];
		}
		else {
			// Independent of previous result[0].
			part_1 = input_32[pos] * key_0 + WordSwap(input_32[pos]) * 0xEF0569FB;
			part_1 = WordSwap(part_1) * 0x76F50F34;
			result[0] = part_1 + WordSwap(part_1) * 0xC3EFEA97;
			result[1] += result[0];

			part_2 = key_0 * (input_32[pos] + result[2]);
			part_2 = 0xB1110000 * part_2 + 0xCF98B111 * (part_2 >> 16);
			part_2 = 0x5B9F0000 * part_2 + 0x87085B9F * (part_2 >> 16);
			part_2 = 0xB96D0000 * part_2 + 0x12CEB96D * (part_2 >> 16);
			part_2 = 0x1D830000 * part_2 + 0x257E1D83 * (part_2 >> 16);
		}
	}
	return { result[0] ^ result[2], result[1] ^ result[3] };
}

inline std::string GecV2Token(std::chrono::system_clock::duration offset)
{
	// algorithm for generating the Sec-MS-GEC v2 token
	// see: https://github.com/rany2/edge-tts/issues/490#issuecomment-6126621790
	// rany2's version in edge-tts, with prefix precomputed:
	//   https://github.com/rany2/edge-tts/pull/492
	// A similar algorithm is used in the UserChoice hash. Following are some implementations.
	// 276793422's reverse engineered version:
	//   https://github.com/276793422/CalcHash_CS64/blob/main/CalcHash_CS64.cpp
	// Gecko's HashString():
	//   https://github.com/mozilla/gecko-dev/blob/master/browser/components/shell/WindowsUserChoice.cpp
	// Chromium's ComputeHash():
	//   https://chromium.googlesource.com/chromium/src/%2B/4a8573cb240df29b0e4d9820303538fb28e31d84/chrome/installer/util/shell_util.cc

	auto str = GecTimestampedToken(offset);
	str.insert(0, GEC_V2_PREFIX, sizeof GEC_V2_PREFIX);

	auto hash = ComputeHash(str);

	// calculate SHA-256 hash
	unsigned char sha_hash[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const unsigned char*>(str.data()), str.size(), sha_hash))
		throw std::runtime_error("GEC token generation failed");

	return HexString(reinterpret_cast<const unsigned char*>(hash.data()), 8) + "-" + HexString(sha_hash, SHA256_DIGEST_LENGTH);
}

}