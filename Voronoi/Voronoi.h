#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <utility>

struct Settings
{
	Settings() : cellSize(3.0f), facetSize(3.0f), minEdgeSize(1.0f), facetDistance(0.5f), extEdgesAddLength(0.1f), shouldGenerateMesh(false),
		generateSticks(true) {}
	std::string modelFile;
	std::string outputMeshFile;
	std::string outputDXFFile;
	float cellSize;
	float facetSize;
	float facetDistance;
	float minEdgeSize;
	float extEdgesAddLength;
	bool shouldGenerateMesh;
	bool generateSticks;
};

using Segment = std::pair<glm::vec3, glm::vec3>;

std::vector<Segment> GenerateVoronoiEdges(const Settings& settings);