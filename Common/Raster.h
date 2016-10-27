#pragma once

#include <cstdint>
#include <vector>

void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void DilateAxial(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void ScaledDilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height, float scale);
void Erode(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);
void Binarize(std::vector<uint8_t>& in, uint8_t threshold);

void ClearNoise(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);

struct Segment
{
	uint32_t val;
	uint32_t count;
	uint32_t xBegin, yBegin;
	uint32_t xEnd, yEnd;
};

void Segmentize(const std::vector<uint8_t>& in, std::vector<uint32_t>& out, std::vector<Segment>& segments,
	const uint32_t width, const uint32_t height, const uint8_t threshold = 1);