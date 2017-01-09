#include "GlContextANGLE.h"

#include <egl/eglext.h>

#include <d3d11.h>
#include <atlbase.h>
#pragma comment (lib, "d3d11.lib")

#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace
{
	void CheckRequiredEGLExtensions(EGLDisplay display);
	void CheckRequiredGLExtensions();
}

GlContextANGLE::GlContextANGLE(uint32_t width, uint32_t height, uint32_t samples) :
width_(width),
height_(height)
{
	if (width == 0 || height == 0)
	{
		throw std::runtime_error("Invalid render target size");
	}

	gl_.display = eglGetDisplay(EGL_D3D11_ONLY_DISPLAY_ANGLE);
	if (!gl_.display)
	{
		throw std::runtime_error("Can't get egl display");
	}

	if (!eglInitialize(gl_.display, nullptr, nullptr))
	{
		throw std::runtime_error("Can't initialize egl");
	}

	CheckRequiredEGLExtensions(gl_.display);

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

	CheckRequiredGLExtensions();
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

std::vector<uint8_t> GlContextANGLE::GetRasterGLES()
{
	const auto FBOBytesPerPixel = 4;

	std::vector<uint8_t> tempPixelBuffer(GetSurfaceWidth() * GetSurfaceHeight() * FBOBytesPerPixel);

	GLint currentFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
	Blit(currentFBO, 0);

	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, GetSurfaceWidth(), GetSurfaceHeight(), GL_RGBA, GL_UNSIGNED_BYTE, tempPixelBuffer.data());
	GL_CHECK();

	std::vector<uint8_t> retVal(GetSurfaceWidth() * GetSurfaceHeight());
	for (auto i = 0u; i < tempPixelBuffer.size(); i += FBOBytesPerPixel)
	{
		retVal[i / FBOBytesPerPixel] = tempPixelBuffer[i];
	}

	glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);
	return retVal;
}

// All extraction & manipulation with underlying d3d11 device here is for performance
// (about 2x faster than glReadPixels on ANGLE).
std::vector<uint8_t> GlContextANGLE::GetRaster()
{
	auto queryDisplayAttribEXT =
		(PFNEGLQUERYDISPLAYATTRIBEXTPROC)eglGetProcAddress("eglQueryDisplayAttribEXT");
	auto queryDeviceAttribEXT =
		(PFNEGLQUERYDEVICEATTRIBEXTPROC)eglGetProcAddress("eglQueryDeviceAttribEXT");
	
	EGLAttrib angleDevice;
	EGLAttrib d3d11Device;
	CHECK(queryDisplayAttribEXT(gl_.display, EGL_DEVICE_EXT, &angleDevice));
	CHECK(queryDeviceAttribEXT(reinterpret_cast<EGLDeviceEXT>(angleDevice),
		EGL_D3D11_DEVICE_ANGLE, &d3d11Device));

	CComPtr<ID3D11Device> device(reinterpret_cast<ID3D11Device*>(d3d11Device));

	CComPtr<ID3D11DeviceContext> context;
	device->GetImmediateContext(&context);

	CComPtr<ID3D11RenderTargetView> rtView;
	context->OMGetRenderTargets(1, &rtView, nullptr);
	CHECK(rtView != nullptr);

	CComPtr<ID3D11Resource> rtResource;
	rtView->GetResource(&rtResource);

	CComPtr<ID3D11Texture2D> resolveTarget;
	CComPtr<ID3D11Texture2D> sysmemTarget;

	CComQIPtr<ID3D11Texture2D> rtTexture(rtResource);
	D3D11_TEXTURE2D_DESC rtDesc;
	rtTexture->GetDesc(&rtDesc);
	rtDesc.MiscFlags = 0;
	rtDesc.BindFlags = 0;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.SampleDesc.Quality = 0;
	CHECK(SUCCEEDED(device->CreateTexture2D(&rtDesc, nullptr, &resolveTarget)));
	context->ResolveSubresource(resolveTarget, 0, rtTexture, 0, rtDesc.Format);

	rtDesc.Usage = D3D11_USAGE_STAGING;
	rtDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	CHECK(SUCCEEDED(device->CreateTexture2D(&rtDesc, nullptr, &sysmemTarget)));
	context->CopyResource(sysmemTarget, resolveTarget);

	std::vector<uint8_t> result(rtDesc.Width * rtDesc.Height);

	D3D11_MAPPED_SUBRESOURCE mapInfo;
	CHECK(SUCCEEDED(context->Map(sysmemTarget, 0, D3D11_MAP_READ, 0, &mapInfo)));
	const auto TextureBytesPerPixel = 4;
	for (size_t y = 0; y < rtDesc.Height; ++y)
	{
		for (size_t x = 0; x < rtDesc.Width; ++x)
		{
			result[rtDesc.Width*y + x] = reinterpret_cast<const uint8_t*>(mapInfo.pData)[mapInfo.RowPitch*y + x*TextureBytesPerPixel];
		}
	}
	context->Unmap(sysmemTarget, 0);
	return result;
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

void GlContextANGLE::ResetFBO()
{
	glBindFramebuffer(GL_FRAMEBUFFER, gl_.fbo.GetHandle());
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

void GlContextANGLE::Blit(GLuint fboFrom, GLuint fboTo)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, fboFrom);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, fboTo);
	glBlitFramebufferANGLE(0, 0, GetSurfaceWidth(), GetSurfaceHeight(),
		0, 0, GetSurfaceWidth(), GetSurfaceHeight(),
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	GL_CHECK();
}

void GlContextANGLE::Blit(const GLFramebuffer& fboFrom, const GLFramebuffer& fboTo)
{
	Blit(fboFrom.GetHandle(), fboTo.GetHandle());
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
void CheckRequiredEGLExtensions(EGLDisplay display)
{
	const std::string eglExtString = eglQueryString(display, EGL_EXTENSIONS);
	const char* requiredEglExtensions[] =
	{
		"EGL_EXT_device_query"
	};
	const auto unsupportedEglExtIt =
		std::find_if(std::begin(requiredEglExtensions), std::end(requiredEglExtensions), [&eglExtString](auto ext) {
		return eglExtString.find(ext) == std::string::npos;
	});

	if (unsupportedEglExtIt != std::end(requiredEglExtensions))
	{
		throw std::runtime_error(std::string("Your system do not support EGL extension: ") + *unsupportedEglExtIt);
	}
}

void CheckRequiredGLExtensions()
{
	const std::string glExtString = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
	const char* requiredGlExtensions[] =
	{
		"GL_EXT_texture_storage",
		"GL_ANGLE_framebuffer_blit",
		"GL_ANGLE_framebuffer_multisample",
		"GL_OES_packed_depth_stencil"
	};

	const auto unsupportedGlExtIt =
		std::find_if(std::begin(requiredGlExtensions), std::end(requiredGlExtensions), [&glExtString](auto ext) {
			return glExtString.find(ext) == std::string::npos;
		});

	if (unsupportedGlExtIt != std::end(requiredGlExtensions))
	{
		throw std::runtime_error(std::string("Your system do not support GL extension: ") + *unsupportedGlExtIt);
	}
}
}
