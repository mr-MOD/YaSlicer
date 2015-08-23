#pragma once
#include <vector>
#include <functional>
#include <cstdint>


void SplitMesh(std::vector<float>& vb, std::vector<uint32_t>& ib, const uint32_t maxVertsInBuffer,
	const std::function<void(const std::vector<float>& vb, const std::vector<uint32_t>& ib)>& onMesh);