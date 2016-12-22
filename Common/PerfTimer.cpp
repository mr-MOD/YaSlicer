#include "PerfTimer.h"

PerfTimer::PerfTimer(const char * name, boost::log::trivial::severity_level level)
	: timerName_(name)
	, startTime_(std::chrono::high_resolution_clock::now())
	, level_(level)
{
}

PerfTimer::~PerfTimer()
{
	const auto duration = std::chrono::high_resolution_clock::now() - startTime_;

	BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(), (::boost::log::keywords::severity = level_))
		<< timerName_ << ": " <<
		std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()/1000.0 << " s";
}
