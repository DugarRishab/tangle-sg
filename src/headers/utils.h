#ifndef UTILS_H
#define UTILS_H

#include <filesystem>
#include <sodium.h>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

std::vector<unsigned char> read_file(const std::string &path);
void write_file(const std::string &path, const std::vector<unsigned char> &data);
std::string to_base64(const unsigned char *bin, size_t len);
std::vector<unsigned char> from_base64(const std::string &b64);

int64_t timeNow(); // Returns current UTC time in milliseconds since epoch

#endif