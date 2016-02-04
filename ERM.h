#pragma once
#include <string>
#include <cstdint>

struct Settings;
void WriteEnvisiontechConfig(const Settings& settings, const std::string& fileName, uint32_t numSlices);