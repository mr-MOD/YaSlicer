#include "Geometry.h"
#include "ErrorHandling.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <iterator>

std::vector<float> CalculateNormals(const std::vector<float>& vb, const std::vector<uint32_t>& ib)
{
	std::vector<float> normals(vb.size(), 0.0f);
	for (size_t i = 0, size = ib.size(); i < size; i += 3)
	{
		auto iA = ib[i + 0];
		auto iB = ib[i + 1];
		auto iC = ib[i + 2];
		glm::vec3 a(vb[iA * 3 + 0], vb[iA * 3 + 1], vb[iA * 3 + 2]);
		glm::vec3 b(vb[iB * 3 + 0], vb[iB * 3 + 1], vb[iB * 3 + 2]);
		glm::vec3 c(vb[iC * 3 + 0], vb[iC * 3 + 1], vb[iC * 3 + 2]);

		auto normal = glm::cross(b - a, c - a);

		normals[iA * 3 + 0] += normal.x;
		normals[iA * 3 + 1] += normal.y;
		normals[iA * 3 + 2] += normal.z;

		normals[iB * 3 + 0] += normal.x;
		normals[iB * 3 + 1] += normal.y;
		normals[iB * 3 + 2] += normal.z;

		normals[iC * 3 + 0] += normal.x;
		normals[iC * 3 + 1] += normal.y;
		normals[iC * 3 + 2] += normal.z;
	}

	for (size_t i = 0, size = normals.size(); i < size; i += 3)
	{
		glm::vec3 n(normals[i + 0], normals[i + 1], normals[i + 2]);
		n = glm::normalize(n);
		normals[i + 0] = n.x;
		normals[i + 1] = n.y;
		normals[i + 2] = n.z;
	}
	return normals;
}

using Edge = uint64_t;

Edge GetEdgeId(uint32_t vertex0, uint32_t vertex1)
{
	uint64_t minIndex;
	uint64_t maxIndex;
	if (vertex0 < vertex1)
	{
		minIndex = vertex0;
		maxIndex = vertex1;
	}
	else
	{
		minIndex = vertex1;
		maxIndex = vertex0;
	}
	return minIndex | (maxIndex << 32);
}

struct FaceList
{
	boost::container::small_vector<uint32_t, 2> faces;
};

using EdgeFacesIncidenceMap = std::vector<std::pair<Edge, FaceList>>;
auto FindEdgeData(EdgeFacesIncidenceMap& edgeMap, Edge edge)
{
	return std::lower_bound(edgeMap.begin(), edgeMap.end(), edge,
		[](const auto& val, const auto& el) { return val.first < el; });
}

void PushFace(EdgeFacesIncidenceMap& edgeStorage, Edge edge, uint32_t faceIndex)
{
	auto it = FindEdgeData(edgeStorage, edge);
	auto& faceList = it->second;
	faceList.faces.push_back(faceIndex);
}

std::vector<AdjacentFaces> BuildFacesAdjacency(const std::vector<uint32_t>& ib)
{
	struct Face
	{
		uint32_t vertex[3];
	};

	auto faces = reinterpret_cast<const Face*>(ib.data());
	const auto faceCount = ib.size() / 3;
	
	EdgeFacesIncidenceMap edgeFacesIncidence;
	edgeFacesIncidence.reserve(faceCount * 3);
	for (size_t i = 0; i < faceCount; ++i)
	{
		const auto& vertices = faces[i].vertex;
		edgeFacesIncidence.emplace_back(GetEdgeId(vertices[0], vertices[1]), FaceList());
		edgeFacesIncidence.emplace_back(GetEdgeId(vertices[1], vertices[2]), FaceList());
		edgeFacesIncidence.emplace_back(GetEdgeId(vertices[2], vertices[0]), FaceList());
	}
	std::sort(edgeFacesIncidence.begin(), edgeFacesIncidence.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });
	edgeFacesIncidence.erase(std::unique(edgeFacesIncidence.begin(), edgeFacesIncidence.end(),
		[](const auto& a, const auto& b) { return a.first == b.first; }), edgeFacesIncidence.end());

	for (size_t i = 0; i < faceCount; ++i)
	{
		const auto& vertices = faces[i].vertex;
	
		const auto edge0 = GetEdgeId(vertices[0], vertices[1]);
		const auto edge1 = GetEdgeId(vertices[1], vertices[2]);
		const auto edge2 = GetEdgeId(vertices[2], vertices[0]);

		PushFace(edgeFacesIncidence, edge0, static_cast<uint32_t>(i));
		PushFace(edgeFacesIncidence, edge1, static_cast<uint32_t>(i));
		PushFace(edgeFacesIncidence, edge2, static_cast<uint32_t>(i));
	}
	
	std::vector<AdjacentFaces> result;
	result.resize(faceCount);

	const auto faceVertexCount = _countof(faces[0].vertex);
	std::vector<uint32_t> adjacentFaces;
	for (size_t i = 0; i < faceCount; ++i)
	{
		adjacentFaces.clear();
		const auto& vertices = faces[i].vertex;
		for (auto n = 0; n < faceVertexCount; ++n)
		{
			const auto edge = GetEdgeId(vertices[n], vertices[(n + 1) % faceVertexCount]);

			const auto& facesShareEdge = FindEdgeData(edgeFacesIncidence, edge)->second;
			adjacentFaces.insert(adjacentFaces.end(), std::begin(facesShareEdge.faces), std::end(facesShareEdge.faces));
		}

		adjacentFaces.erase(std::remove(adjacentFaces.begin(), adjacentFaces.end(), i), adjacentFaces.end());
		auto& currentResult = result[i];
		std::copy(adjacentFaces.begin(), adjacentFaces.end(), std::back_inserter(currentResult.faces));
	}

	return result;
}

class RemapBuilder
{
public:
	RemapBuilder(const uint32_t maxVertices) : maxVertices_(maxVertices) {}

	bool AddFace(uint32_t v0, uint32_t v1, uint32_t v2)
	{
		const bool canAddFace =
			verticesInUse_.size() +
			(verticesInUse_.count(v0) ? 0 : 1) +
			(verticesInUse_.count(v1) ? 0 : 1) +
			(verticesInUse_.count(v2) ? 0 : 1) <= maxVertices_;

		if (!canAddFace)
		{
			return false;
		}

		verticesInUse_.insert(v0);
		verticesInUse_.insert(v1);
		verticesInUse_.insert(v2);

		ib_.push_back(v0);
		ib_.push_back(v1);
		ib_.push_back(v2);

		return true;
	}

	std::unordered_map<uint32_t, uint32_t> BuildRemap()
	{
		std::unordered_map<uint32_t, uint32_t> remap;
		remap.reserve(verticesInUse_.size());
		
		uint32_t vertexCounter = 0;
		for (const auto& v : verticesInUse_)
		{
			remap[v] = vertexCounter++;
		}
		return remap;
	}

	const std::vector<uint32_t>& GetIB() const { return ib_;  }

	void Clear()
	{
		verticesInUse_.clear();
		ib_.clear();
	}

private:
	const uint32_t maxVertices_;
	std::unordered_set<uint32_t> verticesInUse_;
	std::vector<uint32_t> ib_;
};

void MakeMesh(const std::vector<float>& vb, const std::vector<float>& nb, const std::vector<uint32_t>& ib,
	const std::unordered_map<uint32_t, uint32_t>& mapOldToNewIndex,
	const std::function<void(const std::vector<float>& vb, const std::vector<float>& nb, const std::vector<uint32_t>& ib)>& onMesh)
{
	std::vector<float> meshVb;
	std::vector<float> meshNb;
	std::vector<uint32_t> meshIb;

	meshVb.resize(mapOldToNewIndex.size() * 3);
	meshNb.resize(mapOldToNewIndex.size() * 3);
	meshIb.reserve(ib.size());

	for (const auto& v : mapOldToNewIndex)
	{
		std::copy(vb.begin() + v.first * 3, vb.begin() + v.first * 3 + 3, meshVb.begin() + v.second * 3);
		std::copy(nb.begin() + v.first * 3, nb.begin() + v.first * 3 + 3, meshNb.begin() + v.second * 3);
	}

	for (const auto oldIdx : ib)
	{
		meshIb.push_back(mapOldToNewIndex.at(oldIdx));
	}

	onMesh(meshVb, meshNb, meshIb);
}

std::vector<std::vector<uint32_t>> BuildLayers(const std::vector<float>& vb, const std::vector<uint32_t>& ib, int layerCount)
{
	layerCount = std::max(1, layerCount);
	if (layerCount == 1)
	{
		return std::vector<std::vector<uint32_t>>(1, ib);
	}

	auto verticesBegin = reinterpret_cast<const glm::vec3*>(vb.data());
	auto verticesEnd = verticesBegin + vb.size() / 3;
	auto meshMinMaxZ = std::minmax_element(verticesBegin, verticesEnd, [](const auto& a, const auto& b) {
		return a.z < b.z;
	});

	const auto layerHeight = (meshMinMaxZ.second->z - meshMinMaxZ.first->z) / layerCount;

	std::vector<std::vector<uint32_t>> result(layerCount + 1);
	const auto crossLayerIndex = layerCount;
	for (size_t i = 0, size = ib.size(); i < size; i += 3)
	{
		const uint32_t v[] = { ib[i + 0], ib[i + 1], ib[i + 2] };
		const auto minMaxItPair = std::minmax_element(std::begin(v), std::end(v),
			[verts = verticesBegin](const auto& a, const auto& b) { return verts[a].z < verts[b].z; });
		const auto faceMinZ = verticesBegin[*minMaxItPair.first].z;
		const auto faceMaxZ = verticesBegin[*minMaxItPair.second].z;

		const auto layerNumberMin = std::min(layerCount - 1, static_cast<int>((faceMinZ - meshMinMaxZ.first->z) / layerHeight));
		const auto layerNumberMax = std::min(layerCount - 1, static_cast<int>((faceMaxZ - meshMinMaxZ.first->z) / layerHeight));

		const auto layerNumber = layerNumberMin == layerNumberMax ? layerNumberMin : crossLayerIndex;

		auto& layerIb = result[layerNumber];
		layerIb.insert(layerIb.end(), std::begin(v), std::end(v));
	}

	return result;
}

void SplitMesh(std::vector<float>& vb, std::vector<float>& nb, std::vector<uint32_t>& ib, const uint32_t maxVertsInBuffer,
	const MeshCallback& onMesh)
{
	const auto LayerCount = 5;
	const auto layersIb = BuildLayers(vb, ib, LayerCount);

	for (const auto& currentIb : layersIb)
	{
		const auto adjacency = BuildFacesAdjacency(currentIb);

		ASSERT(adjacency.size() == currentIb.size() / 3);

		const bool NotProcessed = false;
		const bool AlreadyProcessed = true;

		std::vector<bool> faceProcessingState(currentIb.size() / 3, NotProcessed);
		std::queue<uint32_t> faceQueue;
		RemapBuilder remapBuilder(maxVertsInBuffer);

		// BFS on face graph
		auto it = faceProcessingState.end();
		while ((it = std::find(faceProcessingState.begin(), faceProcessingState.end(), NotProcessed)) != faceProcessingState.end())
		{
			const auto nextFace = static_cast<uint32_t>(std::distance(faceProcessingState.begin(), it));
			faceQueue.push(nextFace);
			faceProcessingState[nextFace] = AlreadyProcessed;
			while (!faceQueue.empty())
			{
				const auto face = faceQueue.front();
				faceQueue.pop();

				if (!remapBuilder.AddFace(currentIb[face * 3 + 0], currentIb[face * 3 + 1], currentIb[face * 3 + 2]))
				{
					MakeMesh(vb, nb, remapBuilder.GetIB(), remapBuilder.BuildRemap(), onMesh);

					remapBuilder.Clear();
					remapBuilder.AddFace(currentIb[face * 3 + 0], currentIb[face * 3 + 1], currentIb[face * 3 + 2]);
				}

				for (const auto face : adjacency[face].faces)
				{
					if (!faceProcessingState[face])
					{
						faceQueue.push(face);
						faceProcessingState[face] = AlreadyProcessed;
					}
				}
			}
		}

		if (remapBuilder.GetIB().size())
		{
			MakeMesh(vb, nb, remapBuilder.GetIB(), remapBuilder.BuildRemap(), onMesh);
		}
	}
}

void testRemoveVbHoles()
{
	/*std::vector<float> vb{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };
	std::vector<int> ib{ 0, 0, 0, 3, 3, 3 };
	RemoveVbHoles(vb, ib);*/

	/*std::vector<float> vb{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };
	std::vector<int> ib{ 0, 1, 2 };
	RemoveVbHoles(vb, ib);*/

	/*std::vector<float> vb{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };
	std::vector<int> ib{ 2, 2, 2 };
	RemoveVbHoles(vb, ib);*/

	/*std::vector<float> vb;
	std::vector<int> ib;
	RemoveVbHoles(vb, ib);*/

	/*std::vector<float> vb{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9 };
	std::vector<int> ib{ 0, 0, 0, 2, 2, 2, 4, 4, 4, 7, 7, 7, 9, 9, 9 };
	RemoveVbHoles(vb, ib);*/
}


void testSplitMesh()
{
	/*std::vector<float> vb{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };
	std::vector<uint32_t> ib{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };
	SplitMesh(vb, ib, 3, [](const std::vector<float>& vb, const std::vector<uint32_t>& ib){
		int k = 5;
	});*/
}