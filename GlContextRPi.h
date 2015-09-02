#pragma once

#include "GlContext.h"

#include <bcm_host.h>

#include <memory>
#include <vector>

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

class GlContextRPi : public IGlContext
{
public:
	GlContextRPi(uint32_t width, uint32_t height, uint32_t samples);
	~GlContextRPi();
private:

	uint32_t GetSurfaceWidth() const override;
	uint32_t GetSurfaceHeight() const override;
	
	void SwapBuffers() override;

	std::vector<uint8_t> GetRaster() override;
	void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height) override;

	struct GLData
	{
		GLData() : display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT), surface(EGL_NO_SURFACE) {}
		~GLData();

		EGLDisplay display;
		EGLContext context;
		EGLSurface surface;
	};

	BCMHost bcmHost_;
	GLData gl_;
	EGL_DISPMANX_WINDOW_T nativeWindow_;
	std::vector<uint8_t> tempPixelBuffer_;
	std::unique_ptr<RasterSetter> rasterSetter_;

	bool mayHaveNoise_;
};