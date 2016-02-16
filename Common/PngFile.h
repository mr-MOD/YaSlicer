#pragma once

#include <vector>
#include <string>
#include <cstdint>

std::vector<uint8_t> ReadPng(const std::string& fileName,
	uint32_t& width, uint32_t& height, uint32_t& bitsPerPixel);
void WritePng(const std::string& fileName,
	uint32_t width, uint32_t height, uint32_t bitsPerChannel,
	const std::vector<uint8_t>& pixData, const std::vector<uint32_t>& palette = std::vector<uint32_t>());

std::vector<uint32_t> CreateGrayscalePalette();