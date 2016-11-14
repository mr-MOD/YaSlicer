#pragma once

#include <cstdint>
#include <vector>
#include <utility>

void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);

struct Segment
{
	uint32_t val;
	uint32_t count;
	uint32_t xBegin, yBegin;
	uint32_t xEnd, yEnd;
};

void Segmentize(const std::vector<uint8_t>& in, std::vector<uint32_t>& out, std::vector<Segment>& segments,
	const uint32_t width, const uint32_t height, const uint8_t threshold = 1);

float CalculateSegmentArea(const Segment& segment, float physPixelArea,
	const std::vector<uint8_t>& raster, const std::vector<uint32_t>& segmentedRaster, uint32_t width, uint32_t height);

inline std::pair<uint32_t, uint32_t> ExpandRange(uint32_t begin, uint32_t end, uint32_t min, uint32_t max)
{
	return std::make_pair(begin > min ? begin - 1 : begin, end < max ? end + 1 : end);
}

template <typename Action>
void ForEachPixel(const std::pair<uint32_t, uint32_t>& xRange, const std::pair<uint32_t, uint32_t>& yRange, const Action& action)
{
	for (uint32_t y = yRange.first; y < yRange.second; ++y)
	{
		for (uint32_t x = xRange.first; x < xRange.second; ++x)
		{
			action(x, y);
		}
	}
}

template <typename Predicate>
bool AnyOfPixels(const std::pair<uint32_t, uint32_t>& xRange, const std::pair<uint32_t, uint32_t>& yRange, const Predicate& pred)
{
	for (uint32_t y = yRange.first; y < yRange.second; ++y)
	{
		for (uint32_t x = xRange.first; x < xRange.second; ++x)
		{
			if (pred(x, y))
			{
				return true;
			}
		}
	}
	return false;
}