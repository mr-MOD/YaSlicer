#include "GlContextX.h"

#include <stdexcept>
#include <cassert>
#include <cstring>
#include <algorithm>

namespace
{
	void CheckRequiredExtensions();
}

GlContextX::GlContextX(uint32_t width, uint32_t height, uint32_t samples) :
	width_(width),
	height_(height)
{
	if (width == 0 || height == 0)
	{
		throw std::runtime_error("Invalid render target size");
	}

	CreateFullScreenXWindow();

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
		EGL_STENCIL_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};

	EGLConfig config;
	EGLint numConfig;
	if (!eglChooseConfig(gl_.display, attributeList, &config, 1, &numConfig) || numConfig == 0)
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

	gl_.surface = eglCreateWindowSurface(gl_.display, config, x_.window, nullptr);
	if (!gl_.surface)
	{
		throw std::runtime_error("Can't create render surface");
	}

	if (!eglMakeCurrent(gl_.display, gl_.surface, gl_.surface, gl_.context))
	{
		throw std::runtime_error("Can't setup gl context");
	}

	//CheckRequiredExtensions();

	rasterSetter_.reset(new RasterSetter());
}

GlContextX::GLData::~GLData()
{
	if (display)
	{
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}

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

GlContextX::XData::~XData()
{
	if (window)
	{
		XDestroyWindow(display, window);
	}

	if (display)
	{
		XCloseDisplay(display);
	}
}

GlContextX::~GlContextX()
{
}

uint32_t GlContextX::GetSurfaceWidth() const
{
	return width_;
}

uint32_t GlContextX::GetSurfaceHeight() const
{
	return height_;
}

std::vector<uint8_t> GlContextX::GetRaster()
{
	const auto FBOBytesPerPixel = 4;
	if (tempPixelBuffer_.empty())
	{
		tempPixelBuffer_.resize(GetSurfaceWidth() * GetSurfaceHeight() * FBOBytesPerPixel);
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, GetSurfaceWidth(), GetSurfaceHeight(), GL_RGBA, GL_UNSIGNED_BYTE, tempPixelBuffer_.data());
	GL_CHECK();

	std::vector<uint8_t> retVal(GetSurfaceWidth() * GetSurfaceHeight());
	for (auto i = 0u; i < tempPixelBuffer_.size(); i += FBOBytesPerPixel)
	{
		retVal[i / FBOBytesPerPixel] = tempPixelBuffer_[i];
	}

	return retVal;
}

void GlContextX::SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height)
{
	rasterSetter_->SetRaster(raster, width, height);
}

void GlContextX::SwapBuffers()
{
	eglSwapBuffers(gl_.display, gl_.surface);
}

void GlContextX::CreateFullScreenXWindow()
{
	x_.display = XOpenDisplay(nullptr);
	if (!x_.display)
	{
		throw std::runtime_error("Can't get default X display");
	}

	x_.window = XCreateSimpleWindow(x_.display, RootWindow(x_.display, 0), 0, 0, 10, 10,
		0, BlackPixel(x_.display, 0), BlackPixel(x_.display, 0));
	if (!x_.window)
	{
		throw std::runtime_error("Can't create X window");
	}

	Atom wmState = XInternAtom(x_.display, "_NET_WM_STATE", False);
	Atom fullscreen = XInternAtom(x_.display, "_NET_WM_STATE_FULLSCREEN", False);

	XEvent xev;
	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = x_.window;
	xev.xclient.message_type = wmState;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 1;
	xev.xclient.data.l[1] = fullscreen;
	xev.xclient.data.l[2] = 0;

	XMapWindow(x_.display, x_.window);

	XSendEvent(x_.display, DefaultRootWindow(x_.display), False,
		SubstructureRedirectMask | SubstructureNotifyMask, &xev);

	XFlush(x_.display);
}

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	return std::unique_ptr<GlContextX>(new GlContextX(width, height, samples));	
}

std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	assert(false);
	throw std::runtime_error(std::string(__func__) + ": not implemented");
}

namespace
{
	void CheckRequiredExtensions()
	{
		std::string extString = (const char*)glGetString(GL_EXTENSIONS);

		const char* extensions[] =
		{
			"GL_EXT_texture_storage",
			"GL_OES_packed_depth_stencil"
		};

		auto it = std::find_if(std::begin(extensions), std::end(extensions), [&extString](const char* ext) {
			return extString.find(ext) == std::string::npos;
		});

		if (it != std::end(extensions))
		{
			throw std::runtime_error(std::string("Your system do not support extension: ") + *it);
		}
	}
}