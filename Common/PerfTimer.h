#pragma once
#include <string>
#include <chrono>

#include <boost/log/trivial.hpp>

class PerfTimer
{
public:
	PerfTimer(const char* name, boost::log::trivial::severity_level level = boost::log::trivial::info);
	~PerfTimer();
private:
	const std::string timerName_;
	const std::chrono::high_resolution_clock::time_point startTime_;
	const boost::log::trivial::severity_level level_;
};