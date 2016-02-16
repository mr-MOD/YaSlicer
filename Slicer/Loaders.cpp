#include "Loaders.h"
#include "Geometry.h"
#include "CacheOpt.h"

#include <array>
#include <functional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <algorithm>

const auto MaxVerticesPerBuffer = 32000; // Raspberry Pi max

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

		for (auto& el : bucket.storage)
		{
			if (el.first == val.first)
			{
				return std::make_pair(el.second, false);
			}
		}

		bucket.storage.push_back(val);
		return std::make_pair(val.second, true);
	}
private:
	struct Bucket
	{
		std::vector<std::pair<K, V>> storage;
	};
	std::vector<Bucket> buckets_;
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

	HashMerger<Key, uint32_t> vertMerge(std::max(1000u, numTriangles / 100));

#pragma pack(push, 1)
	struct StlTriangle
	{
		float normal[3];
		float vtx0[3];
		float vtx1[3];
		float vtx2[3];
		uint16_t attributes;
	};
#pragma pack(pop)

	static_assert(sizeof(StlTriangle) == 50, "check alignment settings");

	const auto ReadBufferSize = 100 * 1000;
	std::vector<StlTriangle> readBuffer;
	readBuffer.reserve(ReadBufferSize);

	std::vector<uint32_t> indexBuffer;
	indexBuffer.reserve(numTriangles * 3);
	std::vector<float> vertexBuffer;
	vertexBuffer.reserve(numTriangles * 3);

	for (auto triRead = 0u; triRead < numTriangles;)
	{
		auto trianglesToRead = std::min<uint32_t>(ReadBufferSize, numTriangles - triRead);
		readBuffer.resize(trianglesToRead);
		f.read(reinterpret_cast<char*>(readBuffer.data()), trianglesToRead*sizeof(StlTriangle));
		if (f.fail() || f.bad() || f.eof())
		{
			throw std::runtime_error("STL file is corrupted");
		}
		triRead += trianglesToRead;
		
		for (auto it = readBuffer.begin(); it != readBuffer.end(); ++it)
		{
			const auto& tri = *it;
			auto result = vertMerge.insert(
				std::make_pair(Key(tri.vtx0[0], tri.vtx0[1], tri.vtx0[2]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
			if (result.second)
			{
				vertexBuffer.insert(vertexBuffer.end(), std::begin(tri.vtx0), std::end(tri.vtx0));
			}
			indexBuffer.push_back(result.first);

			result = vertMerge.insert(
				std::make_pair(Key(tri.vtx1[0], tri.vtx1[1], tri.vtx1[2]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
			if (result.second)
			{
				vertexBuffer.insert(vertexBuffer.end(), std::begin(tri.vtx1), std::end(tri.vtx1));
			}
			indexBuffer.push_back(result.first);

			result = vertMerge.insert(
				std::make_pair(Key(tri.vtx2[0], tri.vtx2[1], tri.vtx2[2]), static_cast<uint32_t>(vertexBuffer.size() / 3)));
			if (result.second)
			{
				vertexBuffer.insert(vertexBuffer.end(), std::begin(tri.vtx2), std::end(tri.vtx2));
			}
			indexBuffer.push_back(result.first);	
		}
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
			ib.push_back(std::stoi(ind) - 1);
			s >> ind;
			ib.push_back(std::stoi(ind) - 1);
			s >> ind;
			ib.push_back(std::stoi(ind) - 1);
		}
	}
}

void LoadMesh(const std::string& file,
	const std::function<void(const std::vector<float>&, const std::vector<uint16_t>&, uint32_t, uint32_t, uint32_t)>& onMesh)
{
	std::fstream f(file, std::ios::in | std::ios::binary);
	if (f.fail() || f.bad())
	{
		throw std::runtime_error(strerror(errno));
	}

	MeshHeader header;
	if (!f.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
		header.meshSignature != MeshSignature || header.meshVersion > MeshVersion)
	{
		throw std::runtime_error("Invalid mesh file");
	}

	MeshChunkHeader chunkHeader;
	std::vector<float> vb;
	std::vector<uint16_t> ib;
	auto chunk = 0;
	while (f.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader)))
	{
		vb.resize(chunkHeader.vbSize);
		ib.resize(chunkHeader.ibSize);

		f.read(reinterpret_cast<char*>(vb.data()), vb.size()*sizeof(vb[0]));
		f.read(reinterpret_cast<char*>(ib.data()), ib.size()*sizeof(ib[0]));

		onMesh(vb, ib, chunkHeader.frontFacingCount, chunkHeader.orthoFacingCount, chunkHeader.backFacingCount);
	}
}

FileType GetFileType(const std::string& file)
{
	std::string fileLowerCase;
	std::transform(file.begin(), file.end(), std::back_inserter(fileLowerCase), tolower);

	auto dotPos = fileLowerCase.find_last_of('.');
	if (dotPos == std::string::npos || (dotPos + 1) == fileLowerCase.length())
		return FileType::Unknown;

	auto extension = fileLowerCase.substr(dotPos + 1);
	if (extension == "stl")
	{
		return FileType::Stl;
	}
	else if (extension == "obj")
	{
		return FileType::Obj;
	}
	else if (extension == "mesh")
	{
		return FileType::Mesh;
	}

	return FileType::Unknown;
}

void LoadModel(const std::string& file, bool writeMesh, bool optimize, const std::function<void(
	const std::vector<float>&, const std::vector<uint16_t>&, uint32_t, uint32_t, uint32_t)>& onMesh)
{
	const auto fileType = GetFileType(file);

	std::vector<float> vb;
	std::vector<uint32_t> ib;

	switch (fileType)
	{
	case FileType::Mesh:
		LoadMesh(file, onMesh);
		return;
		break;
	case FileType::Stl:
		LoadStl(file, vb, ib);
		break;
	case FileType::Obj:
		LoadObj(file, vb, ib);
		break;
	default:
		throw std::runtime_error("Unknown model file format");
	}

	size_t optimizedVerts = 0;
	size_t totalTris = ib.size() / 3;

	std::string outFileName = file + ".mesh";
	std::fstream outFile(outFileName, std::ios::out | std::ios::binary);
	MeshHeader meshHeader;
	outFile.write(reinterpret_cast<const char*>(&meshHeader), sizeof(meshHeader));

	const auto MaxVertCount = MaxVerticesPerBuffer;
	SplitMesh(vb, ib, MaxVertCount, [optimize, &optimizedVerts, &outFile, &onMesh](const std::vector<float>& vb, const std::vector<uint32_t>& ib) {
		optimizedVerts += vb.size() / 3;

		std::vector<uint16_t> ib16;
		ib16.assign(ib.begin(), ib.end());

		typedef std::array<uint16_t, 3> Triangle;

		auto triBegin = reinterpret_cast<Triangle*>(ib16.data());
		auto triEnd = triBegin + ib16.size() / 3;

		auto frontEndIt = std::partition(triBegin, triEnd, [&vb](const Triangle& tri) {
			auto e1x = vb[tri[1] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e1y = vb[tri[1] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto e2x = vb[tri[2] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e2y = vb[tri[2] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto nz = e1x*e2y - e1y*e2x;

			return nz < 0;
		});
		auto orthoEndIt = std::partition(frontEndIt, triEnd, [&vb](const Triangle& tri) {
			auto e1x = vb[tri[1] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e1y = vb[tri[1] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto e2x = vb[tri[2] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e2y = vb[tri[2] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto nz = e1x*e2y - e1y*e2x;

			return nz == 0;
		});

		auto frontFacing = static_cast<uint32_t>(std::distance(triBegin, frontEndIt) * 3);
		auto orthoFacing = static_cast<uint32_t>(std::distance(frontEndIt, orthoEndIt) * 3);
		auto backFacing = static_cast<uint32_t>(std::distance(orthoEndIt, triEnd) * 3);

		if (optimize)
		{
			auto VertexCacheSize = 32;
			std::vector<uint16_t> ib16Copy(ib16.size());
			Forsyth::OptimizeFaces(ib16.data(), frontFacing,
				static_cast<uint32_t>(vb.size() / 3), ib16Copy.data(), VertexCacheSize);
			Forsyth::OptimizeFaces(ib16.data() + frontFacing + orthoFacing, backFacing,
				static_cast<uint32_t>(vb.size() / 3), ib16Copy.data() + frontFacing + orthoFacing, VertexCacheSize);

			ib16.swap(ib16Copy);
		}


		onMesh(vb, ib16, frontFacing, orthoFacing, backFacing);

		uint32_t vbSize = static_cast<uint32_t>(vb.size());
		uint32_t ibSize = static_cast<uint32_t>(ib16.size());

		outFile.write(reinterpret_cast<const char*>(&vbSize), sizeof(vbSize));
		outFile.write(reinterpret_cast<const char*>(&ibSize), sizeof(ibSize));
		outFile.write(reinterpret_cast<const char*>(&frontFacing), sizeof(frontFacing));
		outFile.write(reinterpret_cast<const char*>(&orthoFacing), sizeof(orthoFacing));
		outFile.write(reinterpret_cast<const char*>(&backFacing), sizeof(backFacing));
		outFile.write(reinterpret_cast<const char*>(vb.data()), vb.size()*sizeof(vb[0]));
		outFile.write(reinterpret_cast<const char*>(ib16.data()), ib16.size()*sizeof(ib16[0]));
	});
}