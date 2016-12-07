#pragma once

#include <cstdint>
#include <vector>
#include <utility>

void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int width, int height);

struct Segment
{
	uint32_t val;
	uint32_t count;
	int xBegin, yBegin;
	int xEnd, yEnd;
};

void Segmentize(const std::vector<uint8_t>& in, std::vector<uint32_t>& out, std::vector<Segment>& segments,
	const int width, const int height, const uint8_t threshold = 1);

float CalculateSegmentArea(const Segment& segment, float physPixelArea,
	const std::vector<uint8_t>& raster, const std::vector<uint32_t>& segmentedRaster, int width, int height);

inline std::pair<int, int> ExpandRange(int begin, int end, int min, int max)
{
	return std::make_pair(begin > min ? begin - 1 : begin, end < max ? end + 1 : end);
}

template <typename Action>
void ForEachPixel(const std::pair<int, int>& xRange, const std::pair<int, int>& yRange, const Action& action)
{
	for (auto y = yRange.first; y < yRange.second; ++y)
	{
		for (auto x = xRange.first; x < xRange.second; ++x)
		{
			action(x, y);
		}
	}
}

template <typename Predicate>
bool AnyOfPixels(const std::pair<int, int>& xRange, const std::pair<int, int>& yRange, const Predicate& pred)
{
	for (auto y = yRange.first; y < yRange.second; ++y)
	{
		for (auto x = xRange.first; x < xRange.second; ++x)
		{
			if (pred(x, y))
			{
				return true;
			}
		}
	}
	return false;
}