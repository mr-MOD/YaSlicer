#include "Renderer.h"
#include "ERM.h"
#include "Utils.h"

#include <PngFile.h>
#include <ErrorHandling.h>

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

void WriteWhiteLayers(const Settings& settings)
{
	const auto outputDir = boost::filesystem::path(settings.outputDir);

	for (uint32_t i = 0; i < settings.whiteLayers; ++i)
	{
		auto filePath = (outputDir / GetOutputFileName(settings, i)).string();

		std::vector<uint8_t> data(settings.renderWidth * settings.renderHeight);
		const uint8_t WhiteColorPaletteIndex = 0xFF;
		std::fill(data.begin(), data.end(), WhiteColorPaletteIndex);
		WritePng(filePath, settings.renderWidth, settings.renderHeight, 8, data, CreateGrayscalePalette());
	}	
}

void RenderModel(Renderer& r, const Settings& settings)
{
	auto tStart = std::chrono::high_resolution_clock::now();
	const auto outputDir = boost::filesystem::path(settings.outputDir);

	WriteWhiteLayers(settings);
	
	uint32_t nSlice = 0;
	uint32_t imageNumber = settings.whiteLayers;
	r.FirstSlice();
	do
	{
		auto filePath = (outputDir / GetOutputFileName(settings, imageNumber++)).string();
		r.SavePng(filePath);

		if (settings.doOverhangAnalysis)
		{
			r.AnalyzeOverhangs();
		}

		if (settings.enableERM)
		{
			r.ERM();
			filePath = (outputDir / GetOutputFileName(settings, imageNumber++)).string();
			r.SavePng(filePath);
		}

		++nSlice;
	} while (r.NextSlice());

	WriteEnvisiontechConfig(settings, "job.cfg", nSlice);

	auto tRender = std::chrono::high_resolution_clock::now();
	auto renderTime = std::chrono::duration_cast<std::chrono::milliseconds>(tRender - tStart).count();
	std::cout << "Render: " << renderTime <<
		" ms, " << (nSlice * 1000.0) / renderTime << " FPS" << std::endl;
}

int main(int argc, char** argv)
{
	try
	{
		Settings settings;
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

			("step", po::value<float>(&settings.step)->default_value(0.025f), "slicing step (mm)")

			("renderWidth", po::value<uint32_t>(&settings.renderWidth)->default_value(1920), "image x resolution")
			("renderHeight", po::value<uint32_t>(&settings.renderHeight)->default_value(1080), "image y resolution")
			("samples", po::value<uint32_t>(&settings.samples)->default_value(8), "samples per pixel")

			("plateWidth", po::value<float>(&settings.plateWidth)->default_value(96.0f), "platform width (mm)")
			("plateHeight", po::value<float>(&settings.plateHeight)->default_value(54.0f), "platform height (mm)")

			("doAxialDilate", po::value<bool>(&settings.doAxialDilate)->default_value(true), "add pixels left and bottom on all contours")

			("doBinarize", po::value<bool>(&settings.doBinarize)->default_value(false), "binarize final image")
			("binarizeThreshold", po::value<uint32_t>(&settings.binarizeThreshold)->default_value(128), "binarization threshold")

			("doOmniDirectionalDilate", po::value<bool>(&settings.doOmniDirectionalDilate)->default_value(false), "extend all contours by 1 pixel")
			("omniDilateSliceFactor", po::value<uint32_t>(&settings.omniDilateSliceFactor)->default_value(2), "do omni directional dilate every N slice")
			("omniDilateScale", po::value<float>(&settings.omniDilateScale)->default_value(1.0f), "scale omni directional extended pixels color by some factor")

			("modelOffsetX", po::value<float>(&settings.modelOffset.x)->default_value(0.0f), "model X offset in pixels")
			("modelOffsetY", po::value<float>(&settings.modelOffset.y)->default_value(0.0f), "model Y offset in pixels")

			("doOverhangAnalysis,a", po::value<bool>(&settings.doOverhangAnalysis)->default_value(false), "analyze unsupported model parts")
			("maxSupportedDistance", po::value<float>(&settings.maxSupportedDistance)->default_value(0.2f), "maximum length of overhang upon previous layer (mm)")

			("enableERM,e", po::value<bool>(&settings.enableERM)->default_value(false), "enable ERM mode")
			("envisiontechTemplatesPath", po::value<std::string>(&settings.envisiontechTemplatesPath)->default_value("envisiontech"), "envisiontech job templates path")

			("queue", po::value<uint32_t>(&settings.queue)->default_value(16), "PNG compression & write queue length (balance CPU-GPU load)")
			("whiteLayers", po::value<uint32_t>(&settings.whiteLayers)->default_value(1), "white layers count")

			("mirrorX", po::value<bool>(&settings.mirrorX)->default_value(false), "mirror image horizontally")
			("mirrorY", po::value<bool>(&settings.mirrorY)->default_value(false), "mirror image vertically")
			;

		po::options_description cmdline_options;
		cmdline_options.add(generic).add(config);

		po::options_description config_file_options;
		config_file_options.add(config);

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
		po::notify(vm);
		po::store(po::parse_config_file<char>(configFile.c_str(), config_file_options), vm);
		po::notify(vm);

		if (vm.count("help") || argc < 2)
		{
			std::cout << "Yarilo slicer v0.81, 2016" << "\n";
			std::cout << cmdline_options << "\n";
			return 0;
		}

		if (settings.modelFile.empty())
		{
			std::cout << "No model to slice, exit" << "\n";
			return 0;
		}

		boost::filesystem::create_directories(settings.outputDir);

		Renderer r(settings);
		RenderModel(r, settings);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}