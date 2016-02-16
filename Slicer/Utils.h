#pragma once
#include <string>
#include <cstdint>
#include <regex>
#include <sstream>
#include <iomanip>

struct Settings;

std::string ReplaceAll(const std::string& str, const std::string& what, const std::string& to);
std::string GetOutputFileName(const Settings& settings, uint32_t slice);