#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <vector>
#include <functional>
#include <cstdint>

#include <boost/container/small_vector.hpp>

void SplitMesh(std::vector<float>& vb, std::vector<float>& nb, std::vector<uint32_t>& ib, const uint32_t maxVertsInBuffer,
	const std::function<void(const std::vector<float>& vb, const std::vector<float>& nb, const std::vector<uint32_t>& ib)>& onMesh);

struct AdjacentFaces
{
	boost::container::small_vector<uint32_t, 3> faces;
};

std::vector<AdjacentFaces> BuildFacesAdjacency(const std::vector<uint32_t>& ib);

std::vector<float> CalculateNormals(const std::vector<float>& vb, const std::vector<uint32_t>& ib);