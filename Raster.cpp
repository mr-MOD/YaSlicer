#include "Raster.h"

#include <algorithm>

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

void Erode(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height)
{
	for (auto y = 1; y < static_cast<int32_t>(height)-1; ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width)-1; ++x)
		{
			auto m = in[y * width + x];
			m = std::min(m, in[(y - 1) * width + (x - 1)]);
			m = std::min(m, in[(y - 1) * width + (x + 0)]);
			m = std::min(m, in[(y - 1) * width + (x + 1)]);
			m = std::min(m, in[(y + 0) * width + (x - 1)]);
			m = std::min(m, in[(y + 0) * width + (x + 1)]);
			m = std::min(m, in[(y + 1) * width + (x - 1)]);
			m = std::min(m, in[(y + 1) * width + (x + 0)]);
			m = std::min(m, in[(y + 1) * width + (x + 1)]);

			out[y * width + x] = m;
		}
	}
}