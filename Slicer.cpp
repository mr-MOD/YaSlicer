#include "Renderer.h"

#include <memory>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>

#include <cstdio>

#ifdef HAVE_LIBBCM_HOST
#include <bcm_host.h>
#endif

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
	if (name == "stlFile")
	{
		std::getline(s, settings.stlFile);
		settings.stlFile = Trim(settings.stlFile);
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
	else if (name == "dilateCount")
	{
		s >> settings.dilateCount;
	}
	else if (name == "downScaleCount")
	{
		s >> settings.downScaleCount;
	}
	else if (name == "dilateSliceFactor")
	{
		s >> settings.dilateSliceFactor;
	}
	else if (name == "modelOffset")
	{
		s >> settings.modelOffset;
	}
	else if (name == "optimizeMesh")
	{
		s >> settings.optimizeMesh;
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

void RenderCommand(std::istream& s, Renderer& r)
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
}

#ifdef HAVE_LIBBCM_HOST
class BCMHost
{
public:
	BCMHost()
	{
		bcm_host_init();
	}
	~BCMHost()
	{
		bcm_host_deinit();
	}
};
#endif


int main(int argc, char** argv)
{
	try
	{
		Settings settings;
		ReadSettings(std::cin, settings);

#ifdef HAVE_LIBBCM_HOST
		BCMHost bcmHost;
#endif
		auto tStart = std::chrono::high_resolution_clock::now();
		
		Renderer r(settings);
		auto tLoad = std::chrono::high_resolution_clock::now();

		std::cout << "Load: " << std::chrono::duration_cast<std::chrono::milliseconds>(tLoad - tStart).count() <<
			" ms" << std::endl;
#ifdef HAVE_LIBBCM_HOST
		while (!std::cin.eof())
		{
			RenderCommand(std::cin, r);
			std::cout << "done" << std::endl;
		}
#else

		tStart = std::chrono::high_resolution_clock::now();
		
		uint32_t nSlice = 0;
		r.FirstSlice();
		do
		{
			char buf[16];
			_snprintf(buf, sizeof(buf), "%05u", nSlice);
			r.SavePng(settings.outputDir + buf + ".png");

			++nSlice;
		}
		while (r.NextSlice());

		auto tRender = std::chrono::high_resolution_clock::now();
		auto renderTime = std::chrono::duration_cast<std::chrono::milliseconds>(tRender - tStart).count();
		std::cout << "Render: " << renderTime <<
			" ms, " << (nSlice * 1000.0) / renderTime << " FPS" << std::endl;
#endif
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}