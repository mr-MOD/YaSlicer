#pragma once

#include <cstdint>
#include <vector>

void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void DilateAxial(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void ScaledDilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height, float scale);
void Erode(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void Binarize(std::vector<uint8_t>& in, uint8_t threshold);

void ClearNoise(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);