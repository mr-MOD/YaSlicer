#pragma once
#include <vector>
#include <string>
#include <cstdint>

void LoadStl(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);
void LoadObj(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);