#include "Renderer.h"
#include "Png.h"
#include "ErrorHandling.h"

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <regex>
#include <codecvt>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

const auto SliceFileDigits = 5;
const char * BasePlateFilename = "base_plate.png";

std::string GetOutputFileName(const Settings& settings, uint32_t slice)
{
	std::stringstream s;
	s << std::setfill('0') << std::setw(SliceFileDigits) << slice << ".png";
	return s.str();
}

std::string GetERMFileName(const Settings& settings, uint32_t slice, bool isPrimary)
{
	std::stringstream s;
	s << "mask_" << std::setfill('0') << std::setw(SliceFileDigits)
		<< (slice*2 + (isPrimary ? 0 : 1)) << (isPrimary ? "_s0" : "_s2") << ".png";
	return s.str();
}

std::string ReplaceAll(const std::string& str, const std::string& what, const std::string& to)
{
	std::regex rx(what);
	return std::regex_replace(str, rx, to);
}

std::string ReadEnvisiontechTemplate(const char* fileName)
{
	boost::filesystem::path templates("envisiontech");

	std::fstream file((templates / fileName).string(), std::ios::in);
	CHECK(file.good());

	std::string result;
	char buf[1024] = { 0 };
	while (!file.eof() && !file.bad())
	{
		auto bytesRead = file.read(buf, sizeof(buf)).gcount();
		result.append(buf, buf + bytesRead);
	}
	CHECK(!file.bad());
	return result;
}

std::string GenLayerConfig(const std::string& layerTemplate, const std::string& layerTemplateERM, const Settings& settings, uint32_t& layerNumber)
{
	const bool isBaseLayer = layerNumber == 0;
	std::string layerFileName;
	if (!isBaseLayer)
	{
		const auto slice = settings.enableERM ? ((layerNumber - 1) / 2) : (layerNumber - 1);
		layerFileName = settings.enableERM ? GetERMFileName(settings, slice, true) : GetOutputFileName(settings, slice);
	}
	else
	{
		layerFileName = BasePlateFilename;
	}

	std::string result;
	result = ReplaceAll(layerTemplate, "#FILENAME#", layerFileName);
	result = ReplaceAll(result, "#LAYER_NUMBER#", std::to_string(layerNumber));
	++layerNumber;

	if (settings.enableERM && !isBaseLayer)
	{
		const auto slice = (layerNumber - 1) / 2;
		layerFileName = GetERMFileName(settings, slice, false);
		result += ReplaceAll(layerTemplateERM, "#FILENAME#", layerFileName);
		result = ReplaceAll(result, "#LAYER_NUMBER#", std::to_string(layerNumber));
		++layerNumber;
	}

	return result;
}

void WriteEnvisiontechConfig(const Settings& settings, const std::string& fileName, uint32_t numSlices)
{
	const auto jobTemplate = ReadEnvisiontechTemplate("job_template.txt");
	const auto baseLayerTemplate = ReadEnvisiontechTemplate("base_layer_template.txt");
	const auto firstLayerTemplate = ReadEnvisiontechTemplate("first_layer_template.txt");
	const auto firstLayerErmTemplate = ReadEnvisiontechTemplate("first_layer_template_erm_part.txt");
	const auto layerTemplate = ReadEnvisiontechTemplate("layer_template.txt");
	const auto layerErmTemplate = ReadEnvisiontechTemplate("layer_template_erm_part.txt");

	uint32_t layerNumber = 0;
	auto baseLayer = GenLayerConfig(baseLayerTemplate, baseLayerTemplate, settings, layerNumber);
	auto firstLayer(numSlices > 0 ? GenLayerConfig(firstLayerTemplate, firstLayerErmTemplate, settings, layerNumber) : std::string());

	std::string layers;
	for (uint32_t slice = 0; slice < (numSlices > 0 ? (numSlices-1) : 0); ++slice)
	{
		layers += GenLayerConfig(layerTemplate, layerErmTemplate, settings, layerNumber);
	}

	auto job = jobTemplate;
	job = ReplaceAll(job, "#TOTAL_LAYERS#", std::to_string(numSlices * (settings.enableERM ? 2 : 1) + 1));
	job = ReplaceAll(job, "#X_RES#", std::to_string(settings.renderWidth));
	job = ReplaceAll(job, "#Y_RES#", std::to_string(settings.renderHeight));
	job = ReplaceAll(job, "#PLATFORM_WIDTH_MICRONS#", std::to_string(static_cast<uint32_t>(settings.plateWidth*1000)));
	job = ReplaceAll(job, "#PLATFORM_HEIGHT_MICRONS#", std::to_string(static_cast<uint32_t>(settings.plateHeight*1000)));
	job = ReplaceAll(job, "#BASE_LAYER#", baseLayer);
	job = ReplaceAll(job, "#FIRST_LAYER#", firstLayer);
	job = ReplaceAll(job, "#LAYERS#", layers);

	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
	std::u16string out = convert.from_bytes(job);

	std::fstream file((boost::filesystem::path(settings.outputDir) / fileName).string(), std::ios::out | std::ios::binary);
	CHECK(file.good());

	const char16_t ByteOrderMark = 0xFEFF;
	file.write(reinterpret_cast<const char*>(&ByteOrderMark), sizeof(ByteOrderMark));
	file.write(reinterpret_cast<const char*>(out.c_str()), out.length() * sizeof(out[0]));
	CHECK(file.good());
}

void RenderModel(Renderer& r, const Settings& settings)
{
	auto tStart = std::chrono::high_resolution_clock::now();
	const auto outputDir = boost::filesystem::path(settings.outputDir);

	auto filePath = (outputDir / BasePlateFilename).string();
	std::vector<uint8_t> data(settings.renderWidth * settings.renderHeight);
	const uint8_t WhiteColorPaletteIndex = 0xFF;
	std::fill(data.begin(), data.end(), WhiteColorPaletteIndex);
	WritePng(filePath, settings.renderWidth, settings.renderHeight, 8, data, CreateGrayscalePalette());

	uint32_t nSlice = 0;
	r.FirstSlice();
	do
	{
		auto filePath = (outputDir / (settings.enableERM ? GetERMFileName(settings, nSlice, true) : GetOutputFileName(settings, nSlice))).string();
		r.SavePng(filePath);

		if (settings.doOverhangAnalysis)
		{
			r.AnalyzeOverhangs();
		}

		if (settings.enableERM)
		{
			r.ERM();
			filePath = (outputDir / GetERMFileName(settings, nSlice, false)).string();
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
			("maxSupportedDistance", po::value<float>(&settings.maxSupportedDistance)->default_value(0.2f), "maximum length of overhang upon previous layer")

			("enableERM,e", po::value<bool>(&settings.enableERM)->default_value(false), "enable ERM mode")

			("queue", po::value<uint32_t>(&settings.queue)->default_value(16), "PNG compression & write queue length (balance CPU-GPU load")

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
			std::cout << "Yarilo slicer v0.8, 2015" << "\n";
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