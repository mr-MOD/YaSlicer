#include "Geometry.h"

#include <cstdint>
#include <unordered_map>
#include <array>
#include <iostream>
#include <algorithm>
#include <numeric>

void RemoveVbHoles(std::vector<float>& vb, std::vector<uint32_t>& ib)
{
	auto vtxCount = vb.size() / 3;
	std::vector<uint8_t> useCount(vtxCount, 0);
	for (auto idx : ib)
	{
		++useCount[idx];
	}

	std::vector<uint32_t> reIndex(vtxCount);
	for (auto i = 0u; i < reIndex.size(); ++i)
	{
		reIndex[i] = i;
	}
	
	size_t totalOffset = 0;
	auto it = useCount.begin();
	while (it != useCount.end())
	{
		auto usedVertsRangeEndIt = std::find_if(it, useCount.end(), [](uint8_t v) {
			return v == 0;
		});
		auto reIndexRangeBeginIt = reIndex.begin() + std::distance(useCount.begin(), it);
		auto reIndexRangeEndIt = reIndex.begin() + std::distance(useCount.begin(), usedVertsRangeEndIt);
		for (auto reIndexIt = reIndexRangeBeginIt; reIndexIt != reIndexRangeEndIt; ++reIndexIt)
		{
			*reIndexIt -= static_cast<uint32_t>(totalOffset);
		}

		auto nextUsedVtxIt = std::find_if(usedVertsRangeEndIt, useCount.end(), [](uint8_t v) {
			return v > 0;
		});
		
		totalOffset += std::distance(usedVertsRangeEndIt, nextUsedVtxIt);
		it = nextUsedVtxIt;
	}
	for (auto& idx : ib)
	{
		idx = reIndex[idx];
	}
	// special remove_if
	typedef std::array<float, 3> Vertex;

	auto result = reinterpret_cast<Vertex*>(vb.data());
	auto vbFirst = reinterpret_cast<Vertex*>(vb.data());
	auto ucFirst = useCount.begin();
	auto ucLast = useCount.end();
	while (ucFirst != ucLast) {
		if (*ucFirst > 0) {
			*result = *vbFirst;
			++result;
		}
		++ucFirst;
		++vbFirst;
	}
	auto eraseStart = vb.begin() + std::distance(reinterpret_cast<Vertex*>(vb.data()), result) * 3;
	vb.erase(eraseStart, vb.end());
}

void SplitMesh(std::vector<float>& vb, std::vector<uint32_t>& ib, const uint32_t maxVertsInBuffer,
	const std::function<void(const std::vector<float>& vb, const std::vector<uint32_t>& ib)>& onMesh)
{
	typedef std::array<uint32_t, 3> Triangle;

	// make last triangle index max of 3
	auto startIt = reinterpret_cast<Triangle*>(ib.data());
	auto endIt = startIt + ib.size() / 3;
	std::for_each(startIt, endIt, [](Triangle& t) {
		if (t[0] > t[1] && t[0] > t[2])
		{
			std::swap(t[0], t[1]);
			std::swap(t[1], t[2]);
		}
		if (t[1] > t[0] && t[1] > t[2])
		{
			std::swap(t[0], t[2]);
			std::swap(t[1], t[2]);
		}
	});

	std::vector<float> curVb;
	curVb.reserve(maxVertsInBuffer * 3);
	std::vector<uint32_t> curIb;
	curIb.reserve(maxVertsInBuffer * 3);

	while (true)
	{
		auto startIt = reinterpret_cast<Triangle*>(ib.data());
		auto endIt = startIt + ib.size() / 3;

		if (startIt == endIt)
		{
			break;
		}

		auto threeVertsRangeEndIt = std::partition(startIt, endIt, [maxVertsInBuffer](const Triangle& tri) {
			return tri[2] < maxVertsInBuffer;
		});

		auto curStepIdxEnd = threeVertsRangeEndIt;
		if (threeVertsRangeEndIt == startIt)
		{
			std::unordered_map<uint32_t, uint32_t> uniqueVertexCounter;
			auto it = startIt;
			for (; it < endIt && uniqueVertexCounter.size() <= maxVertsInBuffer - 3; ++it)
			{
				const auto tri = *it;
				++uniqueVertexCounter[tri[0]];
				++uniqueVertexCounter[tri[1]];
				++uniqueVertexCounter[tri[2]];
			}

			if (uniqueVertexCounter.empty())
			{
				throw std::runtime_error("too complex mesh, can't split");
			}

			auto maxIndex = std::max_element(uniqueVertexCounter.begin(), uniqueVertexCounter.end(),
				[](const std::pair<uint32_t, uint32_t>& v1, const std::pair<uint32_t, uint32_t>& v2) {
					return v1.first < v2.first;
				})->first;

			curStepIdxEnd = it;
			curVb.resize((maxIndex+1) * 3);
		}
		else
		{
			curVb.resize(maxVertsInBuffer * 3);
		}
		
		curIb.assign(reinterpret_cast<uint32_t*>(startIt), reinterpret_cast<uint32_t*>(curStepIdxEnd));
		for (auto idx : curIb)
		{
			curVb[idx * 3 + 0] = vb[idx * 3 + 0];
			curVb[idx * 3 + 1] = vb[idx * 3 + 1];
			curVb[idx * 3 + 2] = vb[idx * 3 + 2];
		}

		RemoveVbHoles(curVb, curIb);
		onMesh(curVb, curIb);

		auto idxCount = std::distance(startIt, curStepIdxEnd) * 3;
		ib.erase(ib.begin(), ib.begin() + idxCount);
		RemoveVbHoles(vb, ib);
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