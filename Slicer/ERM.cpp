#include "ERM.h"
#include "Utils.h"
#include "ErrorHandling.h"
#include "Renderer.h"

#include <fstream>
#include <codecvt>

#include <boost/filesystem.hpp>

std::string ReadEnvisiontechTemplate(const std::string& path)
{
	std::fstream file(path, std::ios::in);
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

std::string ProcessLayerTemplate(const std::string & layerTemplate, const Settings & settings, uint32_t layerNumber)
{
	const auto layerFileName = GetOutputFileName(settings, layerNumber);

	std::string result;
	result = ReplaceAll(layerTemplate, "#FILENAME#", layerFileName);
	result = ReplaceAll(result, "#LAYER_NUMBER#", std::to_string(layerNumber));
	result = ReplaceAll(result, "#LAYER_STEP#", std::to_string(static_cast<uint32_t>(settings.step * 1000)));

	return result;
}

std::string GenLayerConfig(const std::string & layerTemplate, const std::string & layerTemplateERM, const Settings & settings, bool isBaseLayer, uint32_t & layerNumber)
{
	auto result = ProcessLayerTemplate(layerTemplate, settings, layerNumber);
	++layerNumber;

	if (settings.enableERM && !isBaseLayer)
	{
		result += ProcessLayerTemplate(layerTemplateERM, settings, layerNumber);
		++layerNumber;
	}

	return result;
}

void WriteEnvisiontechConfig(const Settings & settings, const std::string & fileName, uint32_t numSlices)
{
	const auto jobTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "job_template.txt").string());
	const auto baseLayerTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "base_layer_template.txt").string());
	const auto firstLayerTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "first_layer_template.txt").string());
	const auto firstLayerErmTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "first_layer_template_erm_part.txt").string());
	const auto layerTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "layer_template.txt").string());
	const auto layerErmTemplate = ReadEnvisiontechTemplate(
		(boost::filesystem::path(settings.envisiontechTemplatesPath) / "layer_template_erm_part.txt").string());

	uint32_t layerNumber = 0;
	std::string baseLayers;
	for (size_t i = 0; i < settings.whiteLayers; ++i)
	{
		baseLayers += GenLayerConfig(baseLayerTemplate, baseLayerTemplate, settings, true, layerNumber);
	}
	
	auto firstLayer(numSlices > 0 ? GenLayerConfig(firstLayerTemplate, firstLayerErmTemplate, settings, false, layerNumber) : std::string());

	std::string layers;
	for (uint32_t slice = 0; slice < (numSlices > 0 ? (numSlices-1) : 0); ++slice)
	{
		layers += GenLayerConfig(layerTemplate, layerErmTemplate, settings, false, layerNumber);
	}

	auto job = jobTemplate;
	job = ReplaceAll(job, "#TOTAL_LAYERS#", std::to_string(numSlices * (settings.enableERM ? 2 : 1) + settings.whiteLayers));
	job = ReplaceAll(job, "#BASE_LAYERS_COUNT#", std::to_string(settings.whiteLayers));
	job = ReplaceAll(job, "#X_RES#", std::to_string(settings.renderWidth));
	job = ReplaceAll(job, "#Y_RES#", std::to_string(settings.renderHeight));
	job = ReplaceAll(job, "#PLATFORM_WIDTH_MICRONS#", std::to_string(static_cast<uint32_t>(settings.plateWidth * 1000)));
	job = ReplaceAll(job, "#PLATFORM_HEIGHT_MICRONS#", std::to_string(static_cast<uint32_t>(settings.plateHeight * 1000)));
	job = ReplaceAll(job, "#BASE_LAYER#", baseLayers);
	job = ReplaceAll(job, "#FIRST_LAYER#", firstLayer);
	job = ReplaceAll(job, "#LAYERS#", layers);

	std::wstring_convert<std::codecvt_utf8_utf16<unsigned short>, unsigned short> convert;
	std::basic_string<unsigned short> out = convert.from_bytes(job);

	std::fstream file((boost::filesystem::path(settings.outputDir) / fileName).string(), std::ios::out | std::ios::binary);
	CHECK(file.good());

	const char16_t ByteOrderMark = 0xFEFF;
	file.write(reinterpret_cast<const char*>(&ByteOrderMark), sizeof(ByteOrderMark));
	file.write(reinterpret_cast<const char*>(out.c_str()), out.length() * sizeof(out[0]));
	CHECK(file.good());
}
