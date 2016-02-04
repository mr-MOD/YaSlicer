#include "Utils.h"

const auto SliceFileDigits = 5;

std::string ReplaceAll(const std::string & str, const std::string & what, const std::string & to)
{
	std::regex rx(what);
	return std::regex_replace(str, rx, to);
}

std::string GetOutputFileName(const Settings & settings, uint32_t slice)
{
	std::stringstream s;
	s << std::setfill('0') << std::setw(SliceFileDigits) << slice << ".png";
	return s.str();
}