#include "GlContextRPi.h"
#include "Raster.h"

#include <stdexcept>
#include <cassert>

GlContextRPi::GlContextRPi(uint32_t width, uint32_t height, uint32_t samples) :
	mayHaveNoise_(true)
{
	gl_.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!gl_.display)
	{
		throw std::runtime_error("Can't get egl display");
	}

	if (!eglInitialize(gl_.display, nullptr, nullptr))
	{
		throw std::runtime_error("Can't initialize egl");
	}

	EGLint const attributeList[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_SAMPLES, static_cast<EGLint>(samples),
		EGL_NONE
	};

	EGLConfig config;
	EGLint numConfig;
	if (!eglSaneChooseConfigBRCM(gl_.display, attributeList, &config, 1, &numConfig) || numConfig == 0)
	{
		throw std::runtime_error("Can't find gl config (check if requested samples count supported)");
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	EGLint contextAttibutes[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	gl_.context = eglCreateContext(gl_.display, config, EGL_NO_CONTEXT, contextAttibutes);
	if (!gl_.context)
	{
		throw std::runtime_error("Can't create gles 2 context");
	}

	VC_RECT_T dst_rect = {};
	VC_RECT_T src_rect = {};

	// create an EGL window surface
	uint32_t screenWidth = 0;
	uint32_t screenHeight = 0;
	if (graphics_get_display_size(0 /* LCD */, &screenWidth, &screenHeight) < 0)
	{
		throw std::runtime_error("Can't get system display");
	}

	if (screenWidth != width || screenHeight != height)
	{
		throw std::runtime_error("Screen resoultion does not match requested");
	}

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = screenWidth;
	dst_rect.height = screenHeight;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = screenWidth << 16;
	src_rect.height = screenHeight << 16;

	auto dispman_display = vc_dispmanx_display_open(0 /* LCD */);
	auto dispman_update = vc_dispmanx_update_start(0);

	auto dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display,
		0/*layer*/, &dst_rect, 0/*src*/,
		&src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, DISPMANX_NO_ROTATE/*transform*/);

	nativeWindow_.element = dispman_element;
	nativeWindow_.width = screenWidth;
	nativeWindow_.height = screenHeight;
	vc_dispmanx_update_submit_sync(dispman_update);
	gl_.surface = eglCreateWindowSurface(gl_.display, config, &nativeWindow_, nullptr);
	if (!gl_.surface)
	{
		throw std::runtime_error("Can't create render surface");
	}

	if (!eglMakeCurrent(gl_.display, gl_.surface, gl_.surface, gl_.context))
	{
		throw std::runtime_error("Can't setup gl context");
	}

	rasterSetter_.reset(new RasterSetter());
}

GlContextRPi::GLData::~GLData()
{
	if (surface != EGL_NO_SURFACE)
	{
		eglDestroySurface(display, surface);
		surface = EGL_NO_SURFACE;
	}

	if (context != EGL_NO_CONTEXT)
	{
		eglDestroyContext(display, context);
		context = EGL_NO_CONTEXT;
	}

	if (display != EGL_NO_DISPLAY)
	{
		eglTerminate(display);
		display = EGL_NO_DISPLAY;
	}
}

GlContextRPi::~GlContextRPi()
{
	eglMakeCurrent(gl_.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

uint32_t GlContextRPi::GetSurfaceWidth() const
{
	return nativeWindow_.width;
}

uint32_t GlContextRPi::GetSurfaceHeight() const
{
	return nativeWindow_.height;
}

void GlContextRPi::SwapBuffers()
{
	if (mayHaveNoise_)
	{
		auto raster = GetRaster();
		decltype(raster) temp(raster.size());
		ClearNoise(raster, temp, GetSurfaceWidth(), GetSurfaceHeight());
		SetRaster(temp, GetSurfaceWidth(), GetSurfaceHeight());
	}

	eglSwapBuffers(gl_.display, gl_.surface);
	mayHaveNoise_ = true;
}

std::vector<uint8_t> GlContextRPi::GetRaster()
{
	const auto FBOBytesPerPixel = 4;
	if (tempPixelBuffer_.empty())
	{
		tempPixelBuffer_.resize(GetSurfaceWidth() * GetSurfaceHeight() * FBOBytesPerPixel);
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, GetSurfaceWidth(), GetSurfaceHeight(), GL_RGBA, GL_UNSIGNED_BYTE, tempPixelBuffer_.data());
	GlCheck("Error reading gl surface data");

	std::vector<uint8_t> retVal(GetSurfaceWidth() * GetSurfaceHeight());
	for (auto i = 0u; i < tempPixelBuffer_.size(); i += FBOBytesPerPixel)
	{
		retVal[i / FBOBytesPerPixel] = tempPixelBuffer_[i];
	}

	/*CRUTCH: RPi have GL driver bugs, leaving junk pixels*/
	decltype(retVal) temp(retVal.size());
	ClearNoise(retVal, temp, GetSurfaceWidth(), GetSurfaceHeight());
	std::swap(retVal, temp);
	/*END CRUTCH*/

	return retVal;
}

void GlContextRPi::SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height)
{
	rasterSetter_->SetRaster(raster, width, height);
	mayHaveNoise_ = false;
}

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	return std::unique_ptr<GlContextRPi>(new GlContextRPi(width, height, samples));
}

std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	assert(false);
	throw std::runtime_error(std::string(__func__) + ": not implemented");
}