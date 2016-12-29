#include "Renderer.h"
#include "ERM.h"
#include "Utils.h"

#include <PngFile.h>
#include <Raster.h>
#include <PerfTimer.h>
#include <ErrorHandling.h>

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <psapi.h>

void WriteWhiteLayers(const Settings& settings, const std::pair<glm::vec2, glm::vec2>& bounds)
{
	const auto outputDir = boost::filesystem::path(settings.outputDir);

	const auto xBorder = settings.basementBorder * settings.renderWidth / settings.plateWidth;
	const auto yBorder = settings.basementBorder * settings.renderHeight / settings.plateHeight;

	const int xStart = std::max(0, static_cast<int>(bounds.first.x - xBorder));
	const int yStart = std::max(0, static_cast<int>(bounds.first.y - yBorder));
	const int xEnd = std::min(static_cast<int>(settings.renderWidth), static_cast<int>(bounds.second.x + xBorder));
	const int yEnd = std::min(static_cast<int>(settings.renderHeight), static_cast<int>(bounds.second.y + yBorder));

	std::vector<uint8_t> data(settings.renderWidth * settings.renderHeight, 0);
	ForEachPixel(std::make_pair(xStart, xEnd), std::make_pair(yStart, yEnd), [&data, &settings](auto x, auto y) {
		const uint8_t WhiteColorPaletteIndex = 0xFF;
		data[y * settings.renderWidth + x] = WhiteColorPaletteIndex;
	});

	const auto palette = CreateGrayscalePalette();
	for (uint32_t i = 0; i < settings.whiteLayers; ++i)
	{
		const auto filePath = (outputDir / GetOutputFileName(settings, i)).string();
		WritePng(filePath, settings.renderWidth, settings.renderHeight, 8, data, palette);
	}	
}

void RenderModel(Renderer& r, const Settings& settings)
{
	PerfTimer renderTime("Render time");
	const auto outputDir = boost::filesystem::path(settings.outputDir);
	
	if (!settings.simulate)
	{
		boost::filesystem::create_directories(settings.outputDir);
		WriteWhiteLayers(settings, r.GetModelProjectionRect());
	}
	
	uint32_t nSlice = 0;
	uint32_t imageNumber = settings.whiteLayers;
	r.FirstSlice();
	do
	{
		auto filePath = (outputDir / GetOutputFileName(settings, imageNumber++)).string();
		r.SavePng(filePath);
	
		if (settings.doOverhangAnalysis)
		{
			r.AnalyzeOverhangs(imageNumber-1);
		}

		if (settings.enableERM)
		{
			r.ERM();
			filePath = (outputDir / GetOutputFileName(settings, imageNumber++)).string();
			r.SavePng(filePath);
		}

		++nSlice;
	} while (r.NextSlice());

	BOOST_LOG_TRIVIAL(info) << "Total slices: " << nSlice;

	if (!settings.simulate)
	{
		WriteEnvisiontechConfig(settings, "job.cfg", nSlice);
	}
}

int main(int argc, char** argv)
{
	try
	{
		Settings settings;
		bool verbose = false;
		std::string configFile;

		namespace po = boost::program_options;
		// Declare a group of options that will be 
		// allowed only on command line
		po::options_description generic("generic options");
		generic.add_options()
			("help,h", "produce help message")
			("config,c", po::value<std::string>(&configFile)->default_value("config.cfg"), "slicing configuration file")
			;

		// Declare a group of options that will be 
		// allowed both on command line and in
		// config file
		po::options_description config("slicing configuration");
		config.add_options()
			("modelFile,m", po::value<std::string>(&settings.modelFile), "model to process")
			("outputDir,o", po::value<std::string>(&settings.outputDir), "output directory")

			("step", po::value<float>(&settings.step)->default_value(settings.step), "slicing step (mm)")

			("renderWidth", po::value<uint32_t>(&settings.renderWidth)->default_value(settings.renderWidth), "image x resolution")
			("renderHeight", po::value<uint32_t>(&settings.renderHeight)->default_value(settings.renderHeight), "image y resolution")
			("samples", po::value<uint32_t>(&settings.samples)->default_value(settings.samples), "samples per pixel")

			("plateWidth", po::value<float>(&settings.plateWidth)->default_value(settings.plateWidth), "platform width (mm)")
			("plateHeight", po::value<float>(&settings.plateHeight)->default_value(settings.plateHeight), "platform height (mm)")

			("doInflate", po::value<bool>(&settings.doInflate)->default_value(settings.doInflate), "inflate model")
			("inflateDistance", po::value<float>(&settings.inflateDistance)->default_value(settings.inflateDistance), "inflate distance (mm)")

			("doSmallSpotsProcessing", po::value<bool>(&settings.doSmallSpotsProcessing)->default_value(settings.doSmallSpotsProcessing), "detect & process small spots")
			("smallSpotThreshold", po::value<float>(&settings.smallSpotThreshold)->default_value(settings.smallSpotThreshold), "maximum small spot area (mm^2)")
			("smallSpotInflateDistance", po::value<float>(&settings.smallSpotInflateDistance)->default_value(settings.smallSpotInflateDistance), "small spot inflate distance (mm)")

			("doOverhangAnalysis,a", po::value<bool>(&settings.doOverhangAnalysis)->default_value(settings.doOverhangAnalysis), "analyze unsupported model parts")
			("maxSupportedDistance", po::value<float>(&settings.maxSupportedDistance)->default_value(settings.maxSupportedDistance), "maximum length of overhang upon previous layer (mm)")

			("enableERM,e", po::value<bool>(&settings.enableERM)->default_value(settings.enableERM), "enable ERM mode")
			("envisiontechTemplatesPath", po::value<std::string>(&settings.envisiontechTemplatesPath)->default_value(settings.envisiontechTemplatesPath), "envisiontech job templates path")

			("queue", po::value<uint32_t>(&settings.queue)->default_value(settings.queue), "PNG compression & write queue length (balance CPU-GPU load)")
			("whiteLayers", po::value<uint32_t>(&settings.whiteLayers)->default_value(settings.whiteLayers), "white layers count")
			("basementBorder", po::value<float>(&settings.basementBorder)->default_value(settings.basementBorder), "basement border size (mm)")

			("mirrorX", po::value<bool>(&settings.mirrorX)->default_value(settings.mirrorX), "mirror image horizontally")
			("mirrorY", po::value<bool>(&settings.mirrorY)->default_value(settings.mirrorY), "mirror image vertically")

			("simulate", po::value<bool>(&settings.simulate)->default_value(settings.simulate), "do not save files")
			("verbose", po::value<bool>(&verbose)->default_value(verbose), "print extended information")
			;

		po::options_description cmdline_options;
		cmdline_options.add(generic).add(config);

		po::options_description config_file_options;
		config_file_options.add(config);

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
		po::notify(vm);

		if (vm.count("help") || argc < 2)
		{
			std::cout << "Yarilo slicer v0.90, 2016" << "\n";
			std::cout << cmdline_options << "\n";
			return 0;
		}

		po::store(po::parse_config_file<char>(configFile.c_str(), config_file_options), vm);
		po::notify(vm);

		if (settings.modelFile.empty())
		{
			std::cout << "No model to slice, exit" << "\n";
			return 0;
		}

		if (!verbose)
		{
			boost::log::core::get()->set_filter
			(
				boost::log::trivial::severity > boost::log::trivial::info
			);
		}

		Renderer r(settings);
		RenderModel(r, settings);

		PROCESS_MEMORY_COUNTERS pmc{};
		GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
		BOOST_LOG_TRIVIAL(info) << "Peak working set: " << pmc.PeakWorkingSetSize / 1024 / 1024 << " MB";
	}
	catch (const std::exception& e)
	{
		BOOST_LOG_TRIVIAL(fatal) << e.what();
		return 1;
	}

	return 0;
}