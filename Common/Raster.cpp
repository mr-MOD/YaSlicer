#include "Raster.h"

#include <ErrorHandling.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <map>


void Dilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height)
{
	for (auto y = 1; y < static_cast<int32_t>(height)-1; ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width)-1; ++x)
		{
			auto m = in[y * width + x];
			m = std::max(m, in[(y - 1) * width + (x - 1)]);
			m = std::max(m, in[(y - 1) * width + (x + 0)]);
			m = std::max(m, in[(y - 1) * width + (x + 1)]);
			m = std::max(m, in[(y + 0) * width + (x - 1)]);
			m = std::max(m, in[(y + 0) * width + (x + 1)]);
			m = std::max(m, in[(y + 1) * width + (x - 1)]);
			m = std::max(m, in[(y + 1) * width + (x + 0)]);
			m = std::max(m, in[(y + 1) * width + (x + 1)]);

			out[y * width + x] = m;
		}
	}
}

void Segmentize(const std::vector<uint8_t>& in, std::vector<uint32_t>& out, std::vector<Segment>& segments,
	const uint32_t width, const uint32_t height, const uint8_t threshold)
{
	ASSERT(in.size() == out.size());
	// TODO: process edges
	std::map<uint32_t, uint32_t> segmentNumberMap;
	segmentNumberMap[0] = 0;

	uint32_t currentSegmentNumber = 1;
	for (auto y = 1; y < static_cast<int32_t>(height); ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width) - 1; ++x)
		{
			if (in[y * width + x] >= threshold)
			{
				const uint32_t samples[] =
				{
					out[(y - 1) * width + x - 1],
					out[(y - 1) * width + x],
					out[(y - 1) * width + x + 1],
					out[y * width + x - 1],
				};

				if (samples[0] == 0 && samples[1] == 0 && samples[2] == 0 && samples[3] == 0)
				{
					out[y * width + x] = currentSegmentNumber;
					segmentNumberMap[currentSegmentNumber] = currentSegmentNumber;
					++currentSegmentNumber;
				}
				else
				{
					const auto max = std::max(std::max(samples[0], samples[1]), std::max(samples[2], samples[3]));
					out[y * width + x] = max;

					for (auto v : samples)
					{
						if (v != 0)
						{
							auto it = segmentNumberMap.find(v);
							it->second = std::max(it->second, max);
						}
					}
				}
			}
			else
			{
				out[y * width + x] = 0;
			}
		}
	}

	bool allMerged = false;
	while (!allMerged)
	{
		allMerged = true;
		for (auto it = segmentNumberMap.begin(); it != segmentNumberMap.end(); ++it)
		{
			if (segmentNumberMap[it->second] != it->second)
			{
				it->second = segmentNumberMap[it->second];
				allMerged = false;
			}
		}
	}

	std::unordered_map<uint32_t, Segment> segmentData;
	for (size_t i = 0; i < out.size(); ++i)
	{
		const auto current = out[i] = segmentNumberMap[out[i]];
		const auto currentX = static_cast<uint32_t>(i) % width;
		const auto currentY = static_cast<uint32_t>(i) / width;
		if (current > 0)
		{
			auto it = segmentData.find(current);
			if (it == segmentData.end())
			{
				it = segmentData.emplace(current, Segment{ current, 0, currentX, currentY, currentX+1, currentY+1}).first;
			}
			++it->second.count;
			it->second.xBegin = std::min(it->second.xBegin, currentX);
			it->second.yBegin = std::min(it->second.yBegin, currentY);
			it->second.xEnd = std::max(it->second.xEnd, currentX+1);
			it->second.yEnd = std::max(it->second.yEnd, currentY+1);
		}
	}

	segments.reserve(segmentData.size());
	std::transform(segmentData.begin(), segmentData.end(), std::back_inserter(segments), [](const auto& v) {
		return v.second;
	});
}

float CalculateSegmentArea(const Segment& segment, float physPixelArea, const std::vector<uint8_t>& raster, const std::vector<uint32_t>& segmentedRaster, uint32_t width, uint32_t height)
{
	const auto xRange = ExpandRange(segment.xBegin, segment.xEnd, 0, width);
	const auto yRange = ExpandRange(segment.yBegin, segment.yEnd, 0, height);

	float area = 0.0f;
	ForEachPixel(xRange, yRange, [&](auto x, auto y)
	{
		const auto currentPixel = raster[y*width + x];

		if (currentPixel > 0 && AnyOfPixels(ExpandRange(x, x + 1, 0, width), ExpandRange(y, y + 1, 0, height),
			[&](auto x, auto y) { return segmentedRaster[y*width + x] == segment.val; }))
		{
			area += physPixelArea * currentPixel / 255.0f;
		}
	});

	return area;
}