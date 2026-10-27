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

}