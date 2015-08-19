#include "Loaders.h"

#include <array>
#include <functional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cerrno>

struct Key
{
	Key(float x, float y, float z) : x(x), y(y), z(z) {}
	bool operator==(const Key& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
	float x, y, z;
};

size_t hash(const Key& k)
{
	std::hash<float> h;
	return h(k.x) ^ h(k.y) ^ h(k.z);
}

template <typename K, typename V>
class HashMerger
{
public:
	HashMerger(size_t buckets) : buckets_(buckets)
	{
	}

	std::pair<V, bool> insert(const std::pair<K, V>& val)
	{
		auto& bucket = buckets_[hash(val.first) % buckets_.size()];
		for (auto& el : bucket)
		{
			if (el.first == val.first)
			{
				return std::make_pair(el.second, false);
			}
		}

		bucket.push_back(val);
		return std::make_pair(val.second, true);
	}
private:
	std::vector<std::vector<std::pair<K,V>>> buckets_;
};


void LoadStl(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib)
{
	std::fstream f(file, std::ios::in | std::ios::binary);
	if (f.fail() || f.bad())
	{
		throw std::runtime_error(strerror(errno));
	}

	char header[80];
	f.read(header, sizeof(header));

	uint32_t numTriangles = 0;
	f.read(reinterpret_cast<char*>(&numTriangles), sizeof(numTriangles));

	float normal[3];
	using Triangle = std::array<float, 3 * 3>;
	Triangle triangleData;
	uint16_t attributes = 0;

	HashMerger<Key, uint32_t> vertMerge(1000 * 1000);

	std::vector<uint32_t> indexBuffer;
	indexBuffer.reserve(numTriangles * 3);
	std::vector<float> vertexBuffer;
	vertexBuffer.reserve(numTriangles * 3);
	for (auto tri = 0u; tri < numTriangles; ++tri)
	{
		f.read(reinterpret_cast<char*>(normal), sizeof(normal));
		f.read(reinterpret_cast<char*>(&triangleData[0]), sizeof(triangleData));
		f.read(reinterpret_cast<char*>(&attributes), sizeof(attributes));

		if (f.fail() || f.bad() || f.eof())
		{
			throw std::runtime_error("STL file is corrupted");
		}

		auto result = vertMerge.insert(
			std::make_pair(Key(triangleData[0], triangleData[1], triangleData[2]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
		if (result.second)
		{
			vertexBuffer.insert(vertexBuffer.end(), triangleData.begin(), triangleData.begin() + 3);
		}
		indexBuffer.push_back(result.first);

		result = vertMerge.insert(
			std::make_pair(Key(triangleData[3], triangleData[4], triangleData[5]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
		if (result.second)
		{
			vertexBuffer.insert(vertexBuffer.end(), triangleData.begin() + 3, triangleData.begin() + 6);
		}
		indexBuffer.push_back(result.first);

		result = vertMerge.insert(
			std::make_pair(Key(triangleData[6], triangleData[7], triangleData[8]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
		if (result.second)
		{
			vertexBuffer.insert(vertexBuffer.end(), triangleData.begin() + 6, triangleData.begin() + 9);
		}
		indexBuffer.push_back(result.first);
	}

	vertexBuffer.swap(vb);
	indexBuffer.swap(ib);
}

void LoadObj(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib)
{
	std::fstream f(file, std::ios::in);
	if (f.fail() || f.bad())
	{
		throw std::runtime_error(strerror(errno));
	}

	std::string type;
	std::string line;
	std::string ind;

	while (!f.eof())
	{
		std::getline(f, line);

		std::istringstream s(line);
		s >> type;
		if (!type.length())
		{
			continue;
		}
		if (type == "v")
		{
			float x, y, z;
			s >> x >> y >> z;
			vb.push_back(x);
			vb.push_back(y);
			vb.push_back(z);
		}
		else if (type == "f")
		{
			s >> ind;
			ib.push_back(std::stoi(ind));
			s >> ind;
			ib.push_back(std::stoi(ind));
			s >> ind;
			ib.push_back(std::stoi(ind));
		}
	}
}