#include "GlContextANGLE.h"

#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace
{
	void CheckRequiredExtensions();
}

GlContextANGLE::GlContextANGLE(uint32_t width, uint32_t height, uint32_t samples) :
width_(width),
height_(height)
{
	if (width == 0 || height == 0)
	{
		throw std::runtime_error("Invalid render target size");
	}

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
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
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

	EGLint surfAttributes[] =
	{
		EGL_WIDTH, static_cast<EGLint>(width),
		EGL_HEIGHT, static_cast<EGLint>(height),
		EGL_NONE
	};
	gl_.surface = eglCreatePbufferSurface(gl_.display, config, surfAttributes);
	if (!gl_.surface)
	{
		throw std::runtime_error("Can't create render surface");
	}

	if (!eglMakeCurrent(gl_.display, gl_.surface, gl_.surface, gl_.context))
	{
		throw std::runtime_error("Can't setup gl context");
	}

	GLint sampleCount = 0;
	glGetIntegerv(GL_MAX_SAMPLES_ANGLE, &sampleCount);
	if (samples > static_cast<uint32_t>(sampleCount))
	{
		throw std::runtime_error("Samples count requested is not supported");
	}

	CheckRequiredExtensions();
	CreateMultisampledFBO(width_, height_, samples);

	glBindFramebuffer(GL_FRAMEBUFFER, gl_.fbo.GetHandle());

	rasterSetter_ = std::make_unique<RasterSetter>();
}

GlContextANGLE::GLData::~GLData()
{
	if (display)
	{
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}

	fbo = GLFramebuffer();
	renderBuffer = GLRenderbuffer();
	renderBufferDepth = GLRenderbuffer();

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

GlContextANGLE::~GlContextANGLE()
{
}

uint32_t GlContextANGLE::GetSurfaceWidth() const
{
	return width_;
}

uint32_t GlContextANGLE::GetSurfaceHeight() const
{
	return height_;
}

std::vector<uint8_t> GlContextANGLE::GetRaster()
{
	const auto FBOBytesPerPixel = 4;
	if (tempPixelBuffer_.empty())
	{
		tempPixelBuffer_.resize(GetSurfaceWidth() * GetSurfaceHeight() * FBOBytesPerPixel);
	}

	Blit(gl_.fbo, GLFramebuffer(0));
	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, GetSurfaceWidth(), GetSurfaceHeight(), GL_RGBA, GL_UNSIGNED_BYTE, tempPixelBuffer_.data());
	GL_CHECK();

	std::vector<uint8_t> retVal(GetSurfaceWidth() * GetSurfaceHeight());
	for (auto i = 0u; i < tempPixelBuffer_.size(); i += FBOBytesPerPixel)
	{
		retVal[i / FBOBytesPerPixel] = tempPixelBuffer_[i];
	}

	glBindFramebuffer(GL_FRAMEBUFFER, gl_.fbo.GetHandle());
	return retVal;
}

void GlContextANGLE::SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height)
{
	rasterSetter_->SetRaster(raster, width, height);
}

void GlContextANGLE::SwapBuffers()
{
	Blit(gl_.fbo, GLFramebuffer(0));
	eglSwapBuffers(gl_.display, gl_.surface);
}

void GlContextANGLE::CreateTextureFBO(GLFramebuffer& fbo, GLTexture& texture)
{
	CreateTextureFBO(GetSurfaceWidth(), GetSurfaceHeight(), fbo, texture);
}

void GlContextANGLE::Resolve(const GLFramebuffer& fboTo)
{
	Blit(gl_.fbo, fboTo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, gl_.fbo.GetHandle());
}

void GlContextANGLE::CreateMultisampledFBO(uint32_t width, uint32_t height, uint32_t samples)
{
	gl_.renderBuffer = GLRenderbuffer::Create();
	glBindRenderbuffer(GL_RENDERBUFFER, gl_.renderBuffer.GetHandle());
	glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, samples, GL_BGRA8_EXT, width, height);
	GL_CHECK();

	gl_.renderBufferDepth = GLRenderbuffer::Create();
	glBindRenderbuffer(GL_RENDERBUFFER, gl_.renderBufferDepth.GetHandle());
	glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8_OES, width, height);
	GL_CHECK();

	gl_.fbo = GLFramebuffer::Create();
	glBindFramebuffer(GL_FRAMEBUFFER, gl_.fbo.GetHandle());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gl_.renderBuffer.GetHandle());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl_.renderBufferDepth.GetHandle());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gl_.renderBufferDepth.GetHandle());
	GL_CHECK();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlContextANGLE::CreateTextureFBO(uint32_t width, uint32_t height, GLFramebuffer& fbo, GLTexture& texture)
{
	texture = GLTexture::Create();
	glBindTexture(GL_TEXTURE_2D, texture.GetHandle());
	glTexStorage2DEXT(GL_TEXTURE_2D, 1, GL_BGRA8_EXT, width, height);
	GL_CHECK();

	fbo = GLFramebuffer::Create();
	glBindFramebuffer(GL_FRAMEBUFFER, fbo.GetHandle());
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.GetHandle(), 0);
	GL_CHECK();

	glBindFramebuffer(GL_FRAMEBUFFER, gl_.fbo.GetHandle());
	glBindTexture(GL_TEXTURE_2D, 0);
}

void GlContextANGLE::Blit(const GLFramebuffer& fboFrom, const GLFramebuffer& fboTo)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, fboFrom.GetHandle());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, fboTo.GetHandle());
	glBlitFramebufferANGLE(0, 0, GetSurfaceWidth(), GetSurfaceHeight(),
		0, 0, GetSurfaceWidth(), GetSurfaceHeight(),
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	GL_CHECK();
}

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	assert(false);
	throw std::runtime_error(__FUNCTION__" not implemented");
}

std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples)
{
	return std::make_unique<GlContextANGLE>(width, height, samples);
}

namespace
{
	void CheckRequiredExtensions()
	{
		std::string extString = (const char*)glGetString(GL_EXTENSIONS);

		const char* extensions[] =
		{
			"GL_EXT_texture_storage",
			"GL_ANGLE_framebuffer_blit",
			"GL_ANGLE_framebuffer_multisample",
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
