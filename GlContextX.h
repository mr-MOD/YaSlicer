#pragma once

#include "GlContext.h"

#include <X11/X.h>
#include <X11/Xlib.h>

class GlContextX : public IGlContext
{
public:
	GlContextX(uint32_t width, uint32_t height, uint32_t samples);
	~GlContextX();
private:

	uint32_t GetSurfaceWidth() const override;
	uint32_t GetSurfaceHeight() const override;

	void SwapBuffers() override;
	std::vector<uint8_t> GetRaster() override;
	void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height) override;

	void CreateFullScreenXWindow();
	struct GLData
	{
		GLData() : display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT), surface(EGL_NO_SURFACE) {}
		~GLData();

		EGLDisplay display;
		EGLContext context;
		EGLSurface surface;
	};

	struct XData
	{
		XData() : display(nullptr), window(0) {}
		~XData();

		Display* display;
		Window window;
	};

	GLData gl_;
	XData x_;

	uint32_t width_;
	uint32_t height_;
	std::vector<uint8_t> tempPixelBuffer_;
	std::unique_ptr<RasterSetter> rasterSetter_;
};