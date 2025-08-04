#include "headers/utils.h"
#include <filesystem>
#include <sodium.h>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept> // for std::runtime_error
#include <iterator> // for std::istreambuf_iterator

using namespace std;

namespace fs = std::filesystem;

// Read a binary file into a vector; returns empty vector on failure
std::vector<unsigned char> read_file(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return {};
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
									  std::istreambuf_iterator<char>());
}

// Write a binary vector to a file
void write_file(const std::string &path, const std::vector<unsigned char> &data)
{
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(data.data()), data.size());
}

std::string to_base64(const unsigned char *bin, size_t len)
{
	// Calculate required buffer size
	size_t encoded_len = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
	std::string b64(encoded_len, '\0');
	sodium_bin2base64(
		b64.data(), b64.size(),
		bin, len,
		sodium_base64_VARIANT_ORIGINAL);
	// Remove any trailing nulls
	b64.resize(std::strlen(b64.c_str()));
	return b64;
}

std::vector<unsigned char> from_base64(const std::string &b64)
{
	std::vector<unsigned char> bin(b64.size());
	size_t bin_len = 0;
	if (sodium_base642bin(
			bin.data(), bin.size(),
			b64.c_str(), b64.size(),
			nullptr, &bin_len, nullptr,
			sodium_base64_VARIANT_ORIGINAL) != 0)
	{
		throw std::runtime_error("Base64 decoding failed");
	}
	bin.resize(bin_len);
	return bin;
}