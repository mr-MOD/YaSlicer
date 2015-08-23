#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

enum class FileType
{
	Stl,
	Obj,
	Mesh,
	Unknown
};

const uint32_t MeshSignature = 'hsem';
const uint16_t MeshVersion = 0x0100;
#pragma pack(push, 1)
struct MeshHeader
{
	MeshHeader() : meshSignature(MeshSignature), meshVersion(MeshVersion) {}
	uint32_t meshSignature;
	uint16_t meshVersion;
};

struct MeshChunkHeader
{
	uint32_t vbSize;
	uint32_t ibSize;
	uint32_t frontFacingCount;
	uint32_t orthoFacingCount;
	uint32_t backFacingCount;
};
#pragma pack(pop)

FileType GetFileType(const std::string& file);

void LoadStl(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);
void LoadObj(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);
void LoadMesh(const std::string& file,
	const std::function<void(const std::vector<float>&, const std::vector<uint16_t>&, uint32_t, uint32_t, uint32_t)>& onMesh);