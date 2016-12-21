#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <vector>
#include <functional>
#include <cstdint>

void SplitMesh(std::vector<float>& vb, std::vector<float>& nb, std::vector<uint32_t>& ib, const uint32_t maxVertsInBuffer,
	const std::function<void(const std::vector<float>& vb, const std::vector<float>& nb, const std::vector<uint32_t>& ib)>& onMesh);

struct AdjacentFaces
{
	uint32_t faces[3];
};

std::vector<AdjacentFaces> BuildFacesAdjacency(const std::vector<uint32_t>& ib);

std::vector<float> CalculateNormals(const std::vector<float>& vb, const std::vector<uint32_t>& ib);