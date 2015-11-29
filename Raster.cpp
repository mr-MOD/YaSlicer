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

void DilateAxial(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height)
{
	for (auto y = 1; y < static_cast<int32_t>(height) - 1; ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width) - 1; ++x)
		{
			auto m = in[y * width + x];	
			m = std::max(m, in[(y - 0) * width + (x - 1)]);
			m = std::max(m, in[(y - 1) * width + (x - 0)]);
			m = std::max(m, in[(y - 1) * width + (x - 1)]);

			out[y * width + x] = m;
		}
	}
}

void ScaledDilate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height, float scale)
{
	for (auto y = 1; y < static_cast<int32_t>(height) - 1; ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width) - 1; ++x)
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

			out[y * width + x] = static_cast<uint8_t>(std::min(255, in[y * width + x] + static_cast<int32_t>(m * scale)));
		}
	}
}

void Binarize(std::vector<uint8_t>& in, uint8_t threshold)
{
	for (auto& v : in)
	{
		v = v >= threshold ? v : 0;
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

void ClearNoise(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height)
{
	for (auto y = 1; y < static_cast<int32_t>(height) - 1; ++y)
	{
		for (auto x = 1; x < static_cast<int32_t>(width) - 1; ++x)
		{
			auto sum = 0u;
			sum += (in[(y - 1) * width + (x - 1)] > 0 ? 1 : 0);
			sum += (in[(y - 1) * width + (x + 0)] > 0 ? 1 : 0);
			sum += (in[(y - 1) * width + (x + 1)] > 0 ? 1 : 0);
			sum += (in[(y + 0) * width + (x - 1)] > 0 ? 1 : 0);
			sum += (in[(y + 0) * width + (x + 1)] > 0 ? 1 : 0);
			sum += (in[(y + 1) * width + (x - 1)] > 0 ? 1 : 0);
			sum += (in[(y + 1) * width + (x + 0)] > 0 ? 1 : 0);
			sum += (in[(y + 1) * width + (x + 1)] > 0 ? 1 : 0);

			if (sum < 2)
			{
				out[y * width + x] = 0;
			}
			else
			{
				out[y * width + x] = in[y * width + x];
			}
		}
	}
}