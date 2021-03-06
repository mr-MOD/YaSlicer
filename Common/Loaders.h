#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

enum class FileType
{
	Stl,
	Obj,
	Unknown
};

FileType GetFileType(const std::string& file);

void LoadStl(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);
void LoadObj(const std::string& file, std::vector<float>& vb, std::vector<uint32_t>& ib);

void LoadModel(const std::string& file, const std::function<void(
	const std::vector<float>&, const std::vector<float>&, const std::vector<uint16_t>&)>& onMesh);