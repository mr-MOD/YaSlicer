#pragma once

#include <cstdint>
#include <vector>

void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void Erode(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);

void ClearNoise(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);