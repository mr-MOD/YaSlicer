/*
 * Renderer.cpp
 *
 *  Created on: Jul 12, 2015
 *      Author: mod
 */

#include "Renderer.h"
#include "Png.h"
#include "Loaders.h"
#include "Geometry.h"
#include "CacheOpt.h"

#include <stdexcept>
#include <numeric>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>

#include <thread>
#include <cerrno>

#undef min
#undef max

#ifdef WIN32
#define ANGLE_ANTIALIASING
#endif

#define SHADER(...) #__VA_ARGS__

#ifdef HAVE_LIBBCM_HOST
const auto MaxTrianglesPerBuffer = 32000u;
#else
const auto MaxTrianglesPerBuffer = 4u * 1000u * 1000u;
#endif

const auto FBOBytesPerPixel = 4;
const auto PNGBytesPerPixel = 1;

const std::string VShader = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	uniform mat4 wvp;
	void main()
	{
		gl_Position = wvp * vec4(vPosition, 1);
	}
);

const std::string FShader = SHADER
(
	precision mediump float;

	void main()
	{
		gl_FragColor = vec4(1);
	}
);

const std::string MaskVShader = SHADER
(
	precision mediump float;
	attribute vec3 vPosition;

	uniform vec2 plateSize;
	uniform mat4 wv;
	uniform mat4 wvp;

	varying vec2 texCoord;
	void main()
	{
		gl_Position = wvp * vec4(vPosition, 1);
		texCoord = ((wv * vec4(vPosition, 1)).xy + plateSize * 0.5) / plateSize;
	}
);

const std::string MaskFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform sampler2D texture;

	void main()
	{
		gl_FragColor = texture2D(texture, texCoord);
	}
);

const std::string FullScreenVShader = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	uniform vec2 texelSize;
	varying vec2 texCoord;
	void main()
	{
		gl_Position = vec4(vPosition, 1);
		texCoord = (vPosition.xy + vec2(1, 1) + texelSize) * 0.5;
	}
);

const std::string DilateFShader = SHADER
(
	precision mediump float;

	varying vec2 texCoord;

	uniform sampler2D texture;
	uniform vec2 texelSize;

	void main()
	{
		vec4 m0, m1, m;
		m0.x = texture2D(texture, texCoord + texelSize*vec2(-1, -1)).r;
		m0.y = texture2D(texture, texCoord + texelSize*vec2(0, -1)).r;
		m0.z = texture2D(texture, texCoord + texelSize*vec2(1, -1)).r;

		m0.w = texture2D(texture, texCoord + texelSize*vec2(-1, 0)).r;
		float c = texture2D(texture, texCoord + texelSize*vec2(0, 0)).r;
		m1.x = texture2D(texture, texCoord + texelSize*vec2(1, 0)).r;

		m1.y = texture2D(texture, texCoord + texelSize*vec2(-1, 1)).r;
		m1.z = texture2D(texture, texCoord + texelSize*vec2(0, 1)).r;
		m1.w = texture2D(texture, texCoord + texelSize*vec2(1, 1)).r;

		m = max(m0, m1);
		m.xy = max(m.xy, m.zw);
		gl_FragColor = vec4(max(max(m.x, m.y), c));
	}
);

const std::string DownScaleFShader[]
{
	"precision mediump float;\n"
	"#extension GL_EXT_shader_texture_lod : enable\n"

	"varying vec2 texCoord;\n"

	"uniform sampler2D texture;\n"
	"uniform vec2 texelSize;\n"

	"uniform float lod;\n"

	"void main() { gl_FragColor = texture2DLodEXT(texture, texCoord, lod); }\n"
};

std::string glErrorString(GLenum err);
void GlCheck(const std::string& s);
void CheckRequiredExtensions(const std::string& extString);
void DilateCPU(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height);

Renderer::GLData::GLData() :
	display(nullptr),
	context(nullptr),
	surface(nullptr),

	mainProgram(0),
	mainVertexPosAttrib(0),
	mainTransformUniform(0),

	maskProgram(0),
	maskVertexPosAttrib(0),
	maskWVTransformUniform(0),
	maskWVPTransformUniform(0),
	maskTextureUniform(0),
	maskPlateSizeUniform(0),

	dilateProgram(0),
	dilateVertexPosAttrib(0),
	dilateTexelSizeUniform(0),
	dilateTextureUniform(0),

	downScaleProgram(0),
	downScaleVertexPosAttrib(0),
	downScaleTexelSizeUniform(0),
	downScaleTextureUniform(0),
	downScaleLodUniform(0),

#ifdef _MSC_VER
	machineMaskTexture( { 0, 0 } ),
#else
	machineMaskTexture{ { 0, 0 } },
#endif
	
	renderTexture(0),
	textureFBO(0),

	renderBuffer(0),
	renderBufferDepth(0),
	fbo(0)
{
}

Renderer::GLData::~GLData()
{
#ifdef ANGLE_ANTIALIASING
	if (fbo)
	{
		glDeleteFramebuffers(1, &fbo);
		fbo = 0;
	}

	if (renderBuffer)
	{
		glDeleteRenderbuffers(1, &renderBuffer);
		renderBuffer = 0;
	}

	if (renderBufferDepth)
	{
		glDeleteRenderbuffers(1, &renderBufferDepth);
		renderBufferDepth = 0;
	}

	if (textureFBO)
	{
		glDeleteFramebuffers(1, &textureFBO);
		textureFBO = 0;
	}

	if (renderTexture)
	{
		glDeleteTextures(1, &renderTexture);
		renderTexture = 0;
	}
#endif
	if (vBuffers.size())
	{
		glDeleteBuffers(static_cast<GLsizei>(vBuffers.size()), &vBuffers[0]);
	}

	if (iBuffers.size())
	{
		glDeleteBuffers(static_cast<GLsizei>(iBuffers.size()), &iBuffers[0]);
	}

	if (machineMaskTexture[0] != 0)
	{
		glDeleteTextures(1, &machineMaskTexture[0]);
	}

	if (machineMaskTexture[1] != 0)
	{
		glDeleteTextures(1, &machineMaskTexture[1]);
	}

	if (mainProgram)
	{
		glDeleteProgram(mainProgram);
		mainProgram = 0;
	}

	if (maskProgram)
	{
		glDeleteProgram(maskProgram);
		maskProgram = 0;
	}

	if (dilateProgram)
	{
		glDeleteProgram(dilateProgram);
		dilateProgram = 0;
	}

	if (downScaleProgram)
	{
		glDeleteProgram(downScaleProgram);
		downScaleProgram = 0;
	}

	if (display)
	{
		eglMakeCurrent( display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
	}

	if (surface)
	{
		eglDestroySurface(display, surface);
		surface = nullptr;
	}

	if (context)
	{
		eglDestroyContext(display, context);
		context = nullptr;
	}

	if (display)
	{
		eglTerminate(display);
		display = nullptr;
	}
}

GLuint CreateShader(GLenum type, const std::string& source)
{
	auto shader = glCreateShader(type);
	const char *sourceArray[1] = { source.c_str() };
	glShaderSource(shader, 1, sourceArray, nullptr);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status)
	{
		GLsizei length;
		std::string infoLog;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		infoLog.resize(length);
		glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.length()), &length, &infoLog[0]);

		glDeleteShader(shader);
		throw std::runtime_error("Shader compilation error: " + infoLog);
	}

	return shader;
}

GLuint CreateProgram(GLuint vertexShader, GLuint fragShader)
{
	auto program = glCreateProgram();

	glAttachShader(program, vertexShader);
	glDeleteShader(vertexShader);

	glAttachShader(program, fragShader);
	glDeleteShader(fragShader);

	glLinkProgram(program);

	GLint status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status)
	{
		GLsizei length;
		std::string infoLog;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
		infoLog.resize(length);
		glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.length()), &length, &infoLog[0]);

		glDeleteProgram(program);
		throw std::runtime_error("Program link error: " + infoLog);
	}

	return program;
}

Renderer::Renderer(const Settings& settings) :
settings_(settings), curMask_(0)
{
	glData_.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!glData_.display)
	{
		throw std::runtime_error("Can't get egl display");
	}

	if (!eglInitialize(glData_.display, nullptr, nullptr))
	{
		throw std::runtime_error("Can't initialize egl");
	}

	EGLint const attributeList[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
#ifndef ANGLE_ANTIALIASING
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
#endif

#ifdef HAVE_LIBBCM_HOST
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_SAMPLES, static_cast<EGLint>(settings_.samples),
#else
		EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
#endif
		EGL_NONE
	};

	EGLConfig config;
	EGLint numConfig;
#ifdef HAVE_LIBBCM_HOST
	if (!eglSaneChooseConfigBRCM(glData_.display, attributeList, &config, 1, &numConfig) || numConfig == 0)
#else
	if (!eglChooseConfig(glData_.display, attributeList, &config, 1, &numConfig) || numConfig == 0)
#endif
	{
		throw std::runtime_error("Can't find gl config (check if requested samples count supported)");
	}

	eglBindAPI (EGL_OPENGL_ES_API);

	EGLint contextAttibutes[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	glData_.context = eglCreateContext(glData_.display, config, EGL_NO_CONTEXT, contextAttibutes);
	if (!glData_.context)
	{
		throw std::runtime_error("Can't create gles 2 context");
	}

#ifdef HAVE_LIBBCM_HOST

	VC_RECT_T dst_rect = {};
	VC_RECT_T src_rect = {};

	// create an EGL window surface
	uint32_t screenWidth = 0;
	uint32_t screenHeight = 0;
	if (graphics_get_display_size(0 /* LCD */, &screenWidth, &screenHeight) < 0)
	{
		throw std::runtime_error("can't get system display");
	}
	std::cout << "Screen: " << screenWidth << "x" << screenHeight << std::endl;

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = screenWidth;
	dst_rect.height = screenHeight;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = screenWidth << 16;
	src_rect.height = screenHeight << 16;

	auto dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
	auto dispman_update = vc_dispmanx_update_start( 0 );

	auto dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
	  0/*layer*/, &dst_rect, 0/*src*/,
	  &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, DISPMANX_NO_ROTATE/*transform*/);

	nativeWindow_.element = dispman_element;
	nativeWindow_.width = screenWidth;
	nativeWindow_.height = screenHeight;
	vc_dispmanx_update_submit_sync( dispman_update );
	glData_.surface = eglCreateWindowSurface(glData_.display, config, &nativeWindow_, nullptr);
#else
	EGLint surfAttributes[] =
	{
		EGL_WIDTH, static_cast<EGLint>(settings_.renderWidth >> settings_.downScaleCount),
		EGL_HEIGHT, static_cast<EGLint>(settings_.renderHeight >> settings_.downScaleCount),
		EGL_NONE
	};
	glData_.surface = eglCreatePbufferSurface(glData_.display, config, surfAttributes);
#endif

	if (!glData_.surface)
	{
		throw std::runtime_error("Can't create render surface");
	}

	if (!eglMakeCurrent(glData_.display, glData_.surface, glData_.surface, glData_.context))
	{
		throw std::runtime_error("Can't setup gl context");
	}

	std::string glExt = (const char*)glGetString(GL_EXTENSIONS);
	CheckRequiredExtensions(glExt);

	GLint size;
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &size);
	std::cout << "Max renderbuffer size: " << size << std::endl;
#ifdef ANGLE_ANTIALIASING
	GLint sampleCount = 0;
	glGetIntegerv(GL_MAX_SAMPLES_ANGLE, &sampleCount);
	std::cout << "Max samples: " << sampleCount << std::endl;

	if (settings_.samples > static_cast<uint32_t>(sampleCount))
	{
		throw std::runtime_error("Samples count requested is not supported");
	}
#endif

	glData_.mainProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, VShader), CreateShader(GL_FRAGMENT_SHADER, FShader));
	glData_.mainTransformUniform = glGetUniformLocation(glData_.mainProgram, "wvp");
	glData_.mainVertexPosAttrib = glGetAttribLocation(glData_.mainProgram, "vPosition");
	GlCheck("Error initializing main shader data");

	glData_.maskProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, MaskVShader), CreateShader(GL_FRAGMENT_SHADER, MaskFShader));
	glData_.maskWVTransformUniform = glGetUniformLocation(glData_.maskProgram, "wv");
	glData_.maskWVPTransformUniform = glGetUniformLocation(glData_.maskProgram, "wvp");
	glData_.maskPlateSizeUniform = glGetUniformLocation(glData_.maskProgram, "plateSize");
	glData_.maskTextureUniform = glGetUniformLocation(glData_.maskProgram, "texture");
	glData_.maskVertexPosAttrib = glGetAttribLocation(glData_.maskProgram, "vPosition");
	GlCheck("Error initializing mask shader data");

#ifndef HAVE_LIBBCM_HOST
	glData_.dilateProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, FullScreenVShader), CreateShader(GL_FRAGMENT_SHADER, DilateFShader));
	glData_.dilateTexelSizeUniform = glGetUniformLocation(glData_.dilateProgram, "texelSize");
	glData_.dilateTextureUniform = glGetUniformLocation(glData_.dilateProgram, "texture");
	glData_.dilateVertexPosAttrib = glGetAttribLocation(glData_.dilateProgram, "vPosition");
	GlCheck("Error initializing dilate shader data");

	std::string downScaleFShaderString;
	for (auto& s : DownScaleFShader) { downScaleFShaderString += s; }
	glData_.downScaleProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, FullScreenVShader), CreateShader(GL_FRAGMENT_SHADER, downScaleFShaderString));
	glData_.downScaleTexelSizeUniform = glGetUniformLocation(glData_.downScaleProgram, "texelSize");
	glData_.downScaleTextureUniform = glGetUniformLocation(glData_.downScaleProgram, "texture");
	glData_.downScaleLodUniform = glGetUniformLocation(glData_.downScaleProgram, "lod");
	glData_.downScaleVertexPosAttrib = glGetAttribLocation(glData_.downScaleProgram, "vPosition");
	GlCheck("Error initializing downscale shader data");
#endif

#ifdef ANGLE_ANTIALIASING
	glGenRenderbuffers(1, &glData_.renderBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, glData_.renderBuffer);
	glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, settings_.samples, GL_BGRA8_EXT,
		settings_.renderWidth, settings_.renderHeight);
	GlCheck("Error setting renderbuffer storage (color)");

	glGenRenderbuffers(1, &glData_.renderBufferDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, glData_.renderBufferDepth);
	glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, settings_.samples, GL_DEPTH24_STENCIL8_OES,
		settings_.renderWidth, settings_.renderHeight);
	GlCheck("Error setting renderbuffer storage (depth-stencil)");

	glGenFramebuffers(1, &glData_.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, glData_.fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, glData_.renderBuffer);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, glData_.renderBufferDepth);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, glData_.renderBufferDepth);
	GlCheck("Error making multisampled framebuffer");

	glGenTextures(1, &glData_.renderTexture);
	glBindTexture(GL_TEXTURE_2D, glData_.renderTexture);
	glTexStorage2DEXT(GL_TEXTURE_2D, 1, GL_BGRA8_EXT, settings_.renderWidth, settings_.renderHeight);
	glGenerateMipmap(GL_TEXTURE_2D);
	GlCheck("Error making render texture");

	glGenFramebuffers(1, &glData_.textureFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, glData_.textureFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glData_.renderTexture, 0);
	GlCheck("Error making texture framebuffer");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
#endif

	glGenTextures(static_cast<GLsizei>(glData_.machineMaskTexture.size()), &glData_.machineMaskTexture[0]);
	for (auto tex : glData_.machineMaskTexture)
	{
		const uint32_t PixelColor = 0xFFFFFFFF;
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &PixelColor);
	}
	GlCheck("Error creating default mask textures");
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(GL_FALSE);

	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);

	std::vector<float> vb;
	std::vector<uint32_t> ib;
	LoadStl(settings_.stlFile, vb, ib);

	auto geometryBytes = vb.size()*sizeof(vb[0]) + ib.size()*sizeof(uint16_t);
	auto surfaceBytes = settings_.renderWidth*settings_.renderHeight * 4 * 2;
	std::cout << "This model requires at least " << (geometryBytes + surfaceBytes) / (1024 * 1024) << " megabytes VRAM" << std::endl;
	
	model_.min.x = model_.min.y = model_.min.z = std::numeric_limits<float>::max();
	model_.max.x = model_.max.y = model_.max.z = std::numeric_limits<float>::lowest();
	for (auto i = 0u; i < vb.size(); i += 3)
	{
		model_.min.x = std::min(model_.min.x, vb[i + 0]);
		model_.min.y = std::min(model_.min.y, vb[i + 1]);
		model_.min.z = std::min(model_.min.z, vb[i + 2]);

		model_.max.x = std::max(model_.max.x, vb[i + 0]);
		model_.max.y = std::max(model_.max.y, vb[i + 1]);
		model_.max.z = std::max(model_.max.z, vb[i + 2]);
	}
	model_.pos = model_.min.z;

	size_t optimizedVerts = 0;
	size_t totalTris = ib.size() / 3;

	const auto MaxVertCount = std::numeric_limits<uint16_t>::max();
	SplitMesh(vb, ib, MaxVertCount, [this, &optimizedVerts](const std::vector<float>& vb, const std::vector<uint32_t>& ib) {
		optimizedVerts += vb.size() / 3;

		auto VertexCacheSize = 32;
		GLData::TriangleData triData;
		
		std::vector<uint16_t> ib16;
		ib16.assign(ib.begin(), ib.end());

		typedef std::array<uint16_t, 3> Triangle;

		auto triBegin = reinterpret_cast<Triangle*>(ib16.data());
		auto triEnd = triBegin + ib16.size()/3;

		auto frontEndIt = std::partition(triBegin, triEnd, [&vb](const Triangle& tri) {
			auto e1x = vb[tri[1] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e1y = vb[tri[1] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto e2x = vb[tri[2] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e2y = vb[tri[2] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto nz = e1x*e2y - e1y*e2x;

			return nz < 0;
		});
		auto orthoEndIt = std::partition(frontEndIt, triEnd, [&vb](const Triangle& tri) {
			auto e1x = vb[tri[1] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e1y = vb[tri[1] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto e2x = vb[tri[2] * 3 + 0] - vb[tri[0] * 3 + 0];
			auto e2y = vb[tri[2] * 3 + 1] - vb[tri[0] * 3 + 1];
			auto nz = e1x*e2y - e1y*e2x;

			return nz == 0;
		});

		triData.frontFacing = static_cast<GLsizei>(std::distance(triBegin, frontEndIt) * 3);
		triData.orthoFacing = static_cast<GLsizei>(std::distance(frontEndIt, orthoEndIt) * 3);
		triData.backFacing = static_cast<GLsizei>(std::distance(orthoEndIt, triEnd) * 3);

		std::vector<uint16_t>& ib16Opt = ib16;
		/*std::vector<uint16_t> ib16Opt(ib16.size());
		Forsyth::OptimizeFaces(ib16.data(),
			static_cast<uint32_t>(triData.frontFacing),
			static_cast<uint32_t>(vb.size() / 3),
			ib16Opt.data(), VertexCacheSize);
		Forsyth::OptimizeFaces(ib16.data() + triData.frontFacing,
			static_cast<uint32_t>(triData.orthoFacing),
			static_cast<uint32_t>(vb.size() / 3),
			ib16Opt.data() + triData.frontFacing, VertexCacheSize);
		Forsyth::OptimizeFaces(ib16.data() + triData.frontFacing + triData.orthoFacing,
			static_cast<uint32_t>(triData.backFacing),
			static_cast<uint32_t>(vb.size() / 3),
			ib16Opt.data() + triData.frontFacing + triData.orthoFacing, VertexCacheSize);*/

		GLuint buffers[2];
		glGenBuffers(2, buffers);

		glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
		glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(vb[0]), vb.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib16Opt.size() * sizeof(ib16Opt[0]), ib16Opt.data(), GL_STATIC_DRAW);

		this->glData_.vBuffers.push_back(buffers[0]);
		this->glData_.iBuffers.push_back(buffers[1]);
		this->glData_.iCount.push_back(triData);
	});

	std::cout
		<< "Source vertices: " << totalTris*3 << "\n"
		<< "Optimized verts: " << optimizedVerts << "\n"
		<< "Total triangles: " << totalTris << std::endl;
	LoadMasks();
}

Renderer::~Renderer()
{
#ifdef WIN32
	for (auto& v : pngSaveResult_)
	{
		v.get();
	}
#endif // WIN32
}

uint32_t Renderer::GetLayersCount() const
{
	return static_cast<uint32_t>((model_.max.z - model_.min.z) / settings_.step + 0.5f);
}

void Renderer::FirstSlice()
{
	model_.pos = model_.min.z + settings_.step;
	Render();
}

bool Renderer::NextSlice()
{
	model_.pos += settings_.step;
	if (model_.pos >= model_.max.z)
	{
		return false;
	}
	Render();
	return true;
}

void Renderer::Black()
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

	eglSwapBuffers(glData_.display, glData_.surface);
}

void Renderer::White()
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

	eglSwapBuffers(glData_.display, glData_.surface);
}

void Renderer::SetMask(uint32_t n)
{
	curMask_ = n;
	Render();
}

void Renderer::Render()
{
	float aspect = settings_.renderWidth / (float)settings_.renderHeight;
	auto middle = (model_.min + model_.max) * 0.5f;
	auto extent = model_.max - model_.min;

	auto offsetX = -(settings_.plateWidth / settings_.renderWidth) * settings_.modelOffset;
	auto offsetY = (settings_.plateHeight / settings_.renderHeight) * settings_.modelOffset;

	auto model = glm::scale(glm::vec3(1.0f, 1.0f, 1.0f)) * glm::translate(offsetX, offsetY, 0.0f);

	auto view = glm::lookAt(glm::vec3(middle.x, middle.y, model_.pos),
		glm::vec3(middle.x, middle.y, model_.max.z + 1.0f),
		glm::vec3(0, 1.0f, 0));
	auto proj = glm::ortho(-settings_.plateHeight * 0.5f * aspect, settings_.plateHeight * 0.5f * aspect,
			-settings_.plateHeight * 0.5f, settings_.plateHeight * 0.5f,
			0.0f, extent.z);

	auto wvMatrix = view * model;
	auto wvpMatrix = proj * view * model;

	GlCheck("Error on frame start");

	
	Model(wvpMatrix);
	Mask(wvpMatrix, wvMatrix);
#ifndef HAVE_LIBBCM_HOST
	if (settings_.dilateCount > 0)
	{
		for (auto i = 0u; i < settings_.dilateCount; ++i)
		{
			Dilate();
		}
	}
	else
	{
		Blit(glData_.fbo, glData_.textureFBO);
	}
	
	Downscale();
#endif
	glFlush();
	glFinish();
	GlCheck("Error rendering frame");

#ifdef HAVE_LIBBCM_HOST
	eglSwapBuffers(glData_.display, glData_.surface);
#endif
}

void Renderer::Model(const glm::mat4x4& wvpMatrix)
{
#ifdef ANGLE_ANTIALIASING
	glBindFramebuffer(GL_FRAMEBUFFER, glData_.fbo);
	GlCheck("Error settings multisampled framebuffer");
#endif

	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearStencil(0x80);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);

	glUseProgram(glData_.mainProgram);
	glUniformMatrix4fv(glData_.mainTransformUniform, 1, GL_FALSE, glm::value_ptr(wvpMatrix));


	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glCullFace(GL_FRONT);
	for (auto i = 0u; i < glData_.vBuffers.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffers[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glData_.iBuffers[i]);
		glDrawElements(GL_TRIANGLES, glData_.iCount[i].orthoFacing + glData_.iCount[i].backFacing,
			GL_UNSIGNED_SHORT, reinterpret_cast<void*>(glData_.iCount[i].frontFacing * sizeof(uint16_t)));
	}

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
	glCullFace(GL_BACK);
	for (auto i = 0u; i < glData_.vBuffers.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffers[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glData_.iBuffers[i]);
		glDrawElements(GL_TRIANGLES, glData_.iCount[i].frontFacing, GL_UNSIGNED_SHORT, nullptr);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GlCheck("Error rendering model");
}

void Renderer::Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix)
{
	glUseProgram(glData_.maskProgram);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	const float quad[] =
	{
		model_.min.x, model_.min.y, model_.max.z,
		model_.min.x, model_.max.y, model_.max.z,
		model_.max.x, model_.max.y, model_.max.z,

		model_.min.x, model_.min.y, model_.max.z,
		model_.max.x, model_.max.y, model_.max.z,
		model_.max.x, model_.min.y, model_.max.z
	};
	glVertexAttribPointer(glData_.maskVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, quad);

	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_LESS, 0x80, 0xFF);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glUniformMatrix4fv(glData_.maskWVTransformUniform, 1, GL_FALSE, glm::value_ptr(wvMatrix));
	glUniformMatrix4fv(glData_.maskWVPTransformUniform, 1, GL_FALSE, glm::value_ptr(wvpMatrix));
	glUniform2f(glData_.maskPlateSizeUniform, settings_.plateWidth, settings_.plateHeight);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, glData_.machineMaskTexture[curMask_]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniform1i(glData_.maskTextureUniform, 0);
	glDrawArrays(GL_TRIANGLES, 0, sizeof(quad) / sizeof(quad[0]) / 3);

	GlCheck("Error rendering mask");
}

void Renderer::Dilate()
{
	Blit(glData_.fbo, glData_.textureFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, glData_.fbo);

	glUseProgram(glData_.dilateProgram);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	const float quad[] =
	{
		-1, -1, 0,
		-1, 1, 0,
		1, 1, 0,

		-1, -1, 0,
		1, 1, 0,
		1, -1, 0
	};
	glVertexAttribPointer(glData_.dilateVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, quad);

	glCullFace(GL_FRONT);
	glDisable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 0, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUniform2f(glData_.dilateTexelSizeUniform, 1.0f / settings_.renderWidth, 1.0f / settings_.renderHeight);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, glData_.renderTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glUniform1i(glData_.dilateTextureUniform, 0);
	glDrawArrays(GL_TRIANGLES, 0, sizeof(quad) / sizeof(quad[0]) / 3);

	GlCheck("Error rendering dilate");
}

void Renderer::Downscale()
{
	const auto targetWidth = settings_.renderWidth >> settings_.downScaleCount;
	const auto targetHeight = settings_.renderHeight >> settings_.downScaleCount;

	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glViewport(0, 0, targetWidth, targetHeight);

	glCullFace(GL_FRONT);
	glDisable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 0, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(glData_.downScaleProgram);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	const float quad[] =
	{
		-1, -1, 0,
		-1, 1, 0,
		1, 1, 0,

		-1, -1, 0,
		1, 1, 0,
		1, -1, 0
	};
	glVertexAttribPointer(glData_.downScaleVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, quad);

	glUniform2f(glData_.downScaleTexelSizeUniform, 1.0f / settings_.renderWidth, 1.0f / settings_.renderHeight);

	glBindTexture(GL_TEXTURE_2D, glData_.renderTexture);
	glGenerateMipmap(GL_TEXTURE_2D);

	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glUniform1i(glData_.downScaleTextureUniform, 0);
	glUniform1f(glData_.downScaleLodUniform, static_cast<float>(settings_.downScaleCount));
	glDrawArrays(GL_TRIANGLES, 0, sizeof(quad) / sizeof(quad[0]) / 3);

	GlCheck("Error downscaling");
}

void Renderer::Blit(GLuint fboFrom, GLuint fboTo)
{
#ifdef ANGLE_ANTIALIASING
	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, fboFrom);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, fboTo);
	glBlitFramebufferANGLE(0, 0, settings_.renderWidth, settings_.renderHeight,
		0, 0, settings_.renderWidth, settings_.renderHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	GlCheck("Error resolving multisampled framebuffer");
#endif
}

#ifdef WIN32
void Renderer::SavePng(const std::string& fileName)
{
	const auto targetWidth = settings_.renderWidth >> settings_.downScaleCount;
	const auto targetHeight = settings_.renderHeight >> settings_.downScaleCount;

	if (tempPixelBuffer_.empty())
	{
		tempPixelBuffer_.resize(targetWidth * targetHeight * FBOBytesPerPixel);
	}
	
	auto pixData = std::make_shared<std::vector<uint8_t>>();
	pixData->resize(targetWidth * targetHeight * PNGBytesPerPixel);

#ifdef ANGLE_ANTIALIASING
	glBindFramebuffer(GL_READ_FRAMEBUFFER_ANGLE, 0);
#endif
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, targetWidth, targetHeight, GL_RGBA, GL_UNSIGNED_BYTE, tempPixelBuffer_.data());

	GlCheck("Error reading gl surface data");
	
	for (auto i = 0u; i < tempPixelBuffer_.size(); i += FBOBytesPerPixel)
	{
		(*pixData)[i / FBOBytesPerPixel] = tempPixelBuffer_[i];
	}

	auto concurrency = settings_.queue;
	bool runOnMainThread = pngSaveResult_.size() > concurrency;

	auto future = std::async([pixData, fileName, targetWidth, targetHeight]() {
		WritePng(fileName, targetWidth, targetHeight, *pixData);
	});
	
	if (runOnMainThread)
	{
		future.get();
		pngSaveResult_.erase(std::remove_if(pngSaveResult_.begin(), pngSaveResult_.end(), [](std::future<void>& v){
			return v.wait_for(std::chrono::milliseconds::zero()) == std::future_status::ready;
		}), pngSaveResult_.end());
	}
	else
	{
		pngSaveResult_.emplace_back(std::move(future));
	}
}
#endif // WIN32



void Renderer::LoadMasks()
{
	for (auto i = 0u; i < settings_.machineMaskFile.size(); ++i)
	{
		if (!settings_.machineMaskFile[i].empty())
		{
			std::cout << "Loading mask " << settings_.machineMaskFile[i] << std::endl;

			uint32_t width = 0, height = 0, bitsPerPixel = 0;
			auto pixelData = ReadPng(settings_.machineMaskFile[i], width, height, bitsPerPixel);

			std::cout << "Mask bits per pixel " << bitsPerPixel << std::endl;

			if (bitsPerPixel != 24)
			{
				throw std::runtime_error("Only support 8 bit per channel RGB (no alpha) PNG files for mask");
			}

			glBindTexture(GL_TEXTURE_2D, glData_.machineMaskTexture[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixelData[0]);

			GlCheck("Error creating texture " + settings_.machineMaskFile[i]);
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

std::string glErrorString(GLenum err)
{
	switch (err)
	{
	case GL_INVALID_ENUM:
		return "invalid enum";
		break;
	case GL_INVALID_VALUE:
		return "invalid value";
		break;
	case GL_INVALID_OPERATION:
		return "invalid operation";
		break;
	case GL_OUT_OF_MEMORY:
		return "out of memory";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		return "framebuffer incomplete attachment";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		return "framebuffer incomplete missing attachment";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
		return "framebuffer incomplete dimensions";
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		return "framebuffer unsupported";
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "invalid framebuffer operation";
		break;
	default:
		return "gl error code " + std::to_string(err);
	}
}

void GlCheck(const std::string& s)
{
	auto err = glGetError();
	auto fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (err != GL_NO_ERROR || fbStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		std::string errText;
		if (err != GL_NO_ERROR)
		{
			errText = "glError: " + glErrorString(err);
		}
		if (fbStatus != GL_FRAMEBUFFER_COMPLETE)
		{
			if (errText.length())
			{
				errText += ", ";
			}
			errText = "FBO status: " + glErrorString(err);
		}
		throw std::runtime_error(s + ". " + errText);
	}
}

void CheckRequiredExtensions(const std::string& extString)
{
	const char* extensions[] =
	{
#ifdef ANGLE_ANTIALIASING
		"GL_EXT_texture_storage",
		"GL_ANGLE_framebuffer_blit",
		"GL_ANGLE_framebuffer_multisample",
		"GL_OES_packed_depth_stencil",
		"GL_EXT_shader_texture_lod",
#endif
		nullptr
	};
	
	auto ext = extensions;
	while (*ext != nullptr)
	{
		if (extString.find(*ext) == std::string::npos)
		{
			throw std::runtime_error(std::string("Your system do not support extension: ") + *ext);
		}
		++ext;
	}
}

uint8_t GetFramePixel(const std::vector<uint8_t>& frame, uint32_t width, uint32_t height, int32_t x, int32_t y)
{
	if (x < 0 || x >= static_cast<int32_t>(width))
		return 0;
	if (y < 0 || y >= static_cast<int32_t>(height))
		return 0;

	return frame[y * width + x];
}

void DilateCPU(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, uint32_t width, uint32_t height)
{
	for (auto y = 0; y < static_cast<int32_t>(height); ++y)
	{
		for (auto x = 0; x < static_cast<int32_t>(width); ++x)
		{
			auto m0 = std::max(GetFramePixel(in, width, height, x, y - 1), GetFramePixel(in, width, height, x, y + 1));
			auto m1 = std::max(GetFramePixel(in, width, height, x - 1, y), GetFramePixel(in, width, height, x + 1, y));
			auto m2 = std::max(GetFramePixel(in, width, height, x - 1, y - 1), GetFramePixel(in, width, height, x - 1, y + 1));
			auto m3 = std::max(GetFramePixel(in, width, height, x + 1, y - 1), GetFramePixel(in, width, height, x + 1, y + 1));

			auto m4 = std::max(m0, m1);
			auto m5 = std::max(m2, m3);

			auto m = std::max(m4, m5);

			out[y * width + x] = m;
		}
	}
}