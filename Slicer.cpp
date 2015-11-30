#include "Renderer.h"
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

const auto SliceFileDigits = 5;
const char * BasePlateFilename = "base_plate.png";

std::string TrimLeft(const std::string& s)
{
	return std::string(std::find_if_not(s.begin(), s.end(), [](char c){ return c <= ' '; }), s.end());
}

std::string TrimRight(const std::string& s)
{
	auto rIt = std::find_if_not(s.rbegin(), s.rend(), [](char c){ return c <= ' '; });
	return std::string(s.begin(), rIt.base());
}

std::string Trim(const std::string& s)
{
	return TrimLeft(TrimRight(s));
}


void ReadSetting(std::istream& s, const std::string& name, Settings& settings)
{
	if (name == "modelFile")
	{
		std::getline(s, settings.modelFile);
		settings.modelFile = Trim(settings.modelFile);
	}
	else if (name == "machineMaskFile1")
	{
		std::getline(s, settings.machineMaskFile[0]);
		settings.machineMaskFile[0] = Trim(settings.machineMaskFile[0]);
	}
	else if (name == "machineMaskFile2")
	{
		std::getline(s, settings.machineMaskFile[1]);
		settings.machineMaskFile[1] = Trim(settings.machineMaskFile[1]);
	}
	else if (name == "outputDir")
	{
		std::getline(s, settings.outputDir);
		settings.outputDir = Trim(settings.outputDir);
		if (settings.outputDir.length() &&
			*settings.outputDir.rbegin() != '/' &&
			*settings.outputDir.rbegin() != '\\')
		{
			settings.outputDir += '/';
		}
	}
	else if (name == "step")
	{
		s >> settings.step;
	}
	else if (name == "renderWidth")
	{
		s >> settings.renderWidth;
	}
	else if (name == "renderHeight")
	{
		s >> settings.renderHeight;
	}
	else if (name == "samples")
	{
		s >> settings.samples;
	}
	else if (name == "plateWidth")
	{
		s >> settings.plateWidth;
	}
	else if (name == "plateHeight")
	{
		s >> settings.plateHeight;
	}
	else if (name == "queue")
	{
		s >> settings.queue;
	}
	else if (name == "doAxialDilate")
	{
		s >> settings.doAxialDilate;
	}
	else if (name == "doOmniDirectionalDilate")
	{
		s >> settings.doOmniDirectionalDilate;
	}
	else if (name == "omniDilateSliceFactor")
	{
		s >> settings.omniDilateSliceFactor;
	}
	else if (name == "omniDilateScale")
	{
		s >> settings.omniDilateScale;
	}
	else if (name == "doBinarize")
	{
		s >> settings.doBinarize;
	}
	else if (name == "binarizeThreshold")
	{
		s >> settings.binarizeThreshold;
	}
	else if (name == "modelOffset")
	{
		s >> settings.modelOffset.x >> settings.modelOffset.y;
	}
	else if (name == "optimizeMesh")
	{
		s >> settings.optimizeMesh;
	}
	else if (name == "offscreen")
	{
		s >> settings.offscreen;
	}
	else if (name == "doOverhangAnalysis")
	{
		s >> settings.doOverhangAnalysis;
	}
	else if (name == "maxSupportedDistance")
	{
		s >> settings.maxSupportedDistance;
	}
	else if (name == "enableERM")
	{
		s >> settings.enableERM;
	}
}

void ReadSettings(std::istream& s, Settings& settings)
{
	std::string name;
	while (name != "cfg_end")
	{
		s >> name;
		ReadSetting(s, name, settings);
	}
}

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
	std::fstream file(std::string("envisiontech/") + fileName, std::ios::in);
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

	std::fstream file(settings.outputDir + fileName, std::ios::out | std::ios::binary);
	CHECK(file.good());

	const char16_t ByteOrderMark = 0xFEFF;
	file.write(reinterpret_cast<const char*>(&ByteOrderMark), sizeof(ByteOrderMark));
	file.write(reinterpret_cast<const char*>(out.c_str()), out.length() * sizeof(out[0]));
	CHECK(file.good());
}

void RenderCommand(std::istream& s, Renderer& r, const Settings& settings)
{
	std::string str;
	s >> str;

	if (str == "NumLayers")
	{
		std::cout << r.GetLayersCount() << std::endl;
	}
	else if (str == "Black")
	{
		r.Black();
	}
	else if (str == "White")
	{
		r.White();
	}
	else if (str == "FirstSlice")
	{
		r.FirstSlice();
	}
	else if (str == "NextSlice")
	{
		r.NextSlice();
	}
	else if (str == "Mask")
	{
		uint32_t mask= 0;
		s >> mask;
		mask = std::min(1u, mask);
		r.SetMask(mask);
	}
	else if (str == "Sleep")
	{
		uint32_t delay = 0;
		s >> delay;
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	}
	else if (str == "MirrorX")
	{
		r.MirrorX();
	}
	else if (str == "MirrorY")
	{
		r.MirrorY();
	}
	else if (str == "SliceModel")
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		r.White();
		auto filePath = settings.outputDir + BasePlateFilename;
		r.SavePng(filePath);

		uint32_t nSlice = 0;
		r.FirstSlice();
		do
		{
			auto filePath = settings.outputDir + (settings.enableERM ? GetERMFileName(settings, nSlice, true) : GetOutputFileName(settings, nSlice));
			r.SavePng(filePath);

			if (settings.doOverhangAnalysis)
			{
				r.AnalyzeOverhangs();
			}

			if (settings.enableERM)
			{
				r.ERM();
				filePath = settings.outputDir + GetERMFileName(settings, nSlice, false);
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
}

int main(int argc, char** argv)
{
	try
	{
		Settings settings;
		ReadSettings(std::cin, settings);

		Renderer r(settings);

		while (!std::cin.eof())
		{
			RenderCommand(std::cin, r, settings);
			std::cout << "done" << std::endl;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}