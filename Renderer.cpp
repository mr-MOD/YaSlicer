/*
 * Renderer.cpp
 *
 *  Created on: Jul 12, 2015
 *      Author: mod
 */

#include "Renderer.h"

#include <stdexcept>
#include <sstream>
#include <numeric>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>

#include <thread>
#include <cerrno>

#include <png.h>

#undef min
#undef max

#define ANGLE_ANTIALIASING
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

void WritePng(const std::string& fileName, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixData);
std::vector<uint8_t> ReadPng(const std::string& fileName, uint32_t& width, uint32_t& height, uint32_t& bitsPerPixel);
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
	if (vBuffersFront.size())
	{
		glDeleteBuffers(static_cast<GLsizei>(vBuffersFront.size()), &vBuffersFront[0]);
	}

	if (vBuffersBack.size())
	{
		glDeleteBuffers(static_cast<GLsizei>(vBuffersBack.size()), &vBuffersBack[0]);
	}

	if (vBuffersVertical.size())
	{
		glDeleteBuffers(static_cast<GLsizei>(vBuffersVertical.size()), &vBuffersVertical[0]);
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

Renderer::ModelData::ModelData() :
pos(), numFront(0), numBack(0), numVertical(0)
{
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

	LoadStl();
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
	for (auto i = 0u, triDrawn = 0u; i < glData_.vBuffersBack.size(); ++i, triDrawn += MaxTrianglesPerBuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffersBack[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		auto numVertices = std::min(model_.numBack - triDrawn, MaxTrianglesPerBuffer) * 3;
		glDrawArrays(GL_TRIANGLES, 0, numVertices);
	}
	for (auto i = 0u, triDrawn = 0u; i < glData_.vBuffersVertical.size(); ++i, triDrawn += MaxTrianglesPerBuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffersVertical[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		auto numVertices = std::min(model_.numVertical - triDrawn, MaxTrianglesPerBuffer) * 3;
		glDrawArrays(GL_TRIANGLES, 0, numVertices);
	}

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
	glCullFace(GL_BACK);
	for (auto i = 0u, triDrawn = 0u; i < glData_.vBuffersFront.size(); ++i, triDrawn += MaxTrianglesPerBuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffersFront[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		auto numVertices = std::min(model_.numFront - triDrawn, MaxTrianglesPerBuffer) * 3;
		glDrawArrays(GL_TRIANGLES, 0, numVertices);
	}
	for (auto i = 0u, triDrawn = 0u; i < glData_.vBuffersVertical.size(); ++i, triDrawn += MaxTrianglesPerBuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffersVertical[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		auto numVertices = std::min(model_.numVertical - triDrawn, MaxTrianglesPerBuffer) * 3;
		glDrawArrays(GL_TRIANGLES, 0, numVertices);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
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

void Renderer::LoadStl()
{
	std::fstream f(settings_.stlFile, std::ios::in | std::ios::binary);
	if (f.fail() || f.bad())
	{
		throw std::runtime_error(strerror(errno));
	}

	char header[80];
	f.read(header, sizeof(header));

	uint32_t numTriangles = 0;
	f.read(reinterpret_cast<char*>(&numTriangles), sizeof(numTriangles));
	std::cout << "Total triangles: " << numTriangles << std::endl;

	auto numParts = numTriangles / MaxTrianglesPerBuffer + 1;

	//model_.geometry.reserve(numTriangles * 3 * 3);

	model_.max = glm::vec3(std::numeric_limits<float>::lowest());
	model_.min = glm::vec3(std::numeric_limits<float>::max());

	float normal[3];
	using Triangle = std::array<float, 3 * 3>;
	Triangle triangleData;
	uint16_t attributes = 0;

	std::vector<float> frontBufData;
	std::vector<float> backBufData;
	std::vector<float> vertBufData;
	frontBufData.reserve(MaxTrianglesPerBuffer * triangleData.size());
	backBufData.reserve(MaxTrianglesPerBuffer * triangleData.size());
	vertBufData.reserve(MaxTrianglesPerBuffer * triangleData.size());

	auto updateBuffers = [](std::vector<float>& bufData, std::vector<GLuint>& buffers, bool force) {
		if (bufData.size() == bufData.capacity() || (force && bufData.size() > 0))
		{
			GLuint glBuffer = 0;
			glGenBuffers(1, &glBuffer);
			glBindBuffer(GL_ARRAY_BUFFER, glBuffer);
			glBufferData(GL_ARRAY_BUFFER, bufData.size() * sizeof(bufData[0]), bufData.data(), GL_STATIC_DRAW);
			bufData.clear();
			buffers.push_back(glBuffer);
		}
	};
	auto processTriangle = [&updateBuffers](const Triangle& triangleData, std::vector<float>& bufData, std::vector<GLuint>& buffers) {
		bufData.insert(bufData.end(), std::begin(triangleData), std::end(triangleData));
		updateBuffers(bufData, buffers, false);
	};
	
	uint32_t numFront = 0;
	uint32_t numBack = 0;
	uint32_t numVertical = 0;

	for (auto tri = 0u; tri < numTriangles; ++tri)
	{
		f.read(reinterpret_cast<char*>(normal), sizeof(normal));
		f.read(reinterpret_cast<char*>(&triangleData[0]), sizeof(triangleData));
		f.read(reinterpret_cast<char*>(&attributes), sizeof(attributes));

		if (f.fail() || f.bad() || f.eof())
		{
			throw std::runtime_error("STL file is corrupted");
		}

		model_.min = glm::min(model_.min,
			glm::vec3(triangleData[0], triangleData[1], triangleData[2]));

		model_.min = glm::min(model_.min,
			glm::vec3(triangleData[3], triangleData[4], triangleData[5]));

		model_.min = glm::min(model_.min,
			glm::vec3(triangleData[6], triangleData[7], triangleData[8]));


		model_.max = glm::max(model_.max,
			glm::vec3(triangleData[0], triangleData[1], triangleData[2]));

		model_.max = glm::max(model_.max,
			glm::vec3(triangleData[3], triangleData[4], triangleData[5]));

		model_.max = glm::max(model_.max,
			glm::vec3(triangleData[6], triangleData[7], triangleData[8]));

		auto e1x = triangleData[1 * 3 + 0] - triangleData[0 * 3 + 0];
		auto e1y = triangleData[1 * 3 + 1] - triangleData[0 * 3 + 1];

		auto e2x = triangleData[2 * 3 + 0] - triangleData[0 * 3 + 0];
		auto e2y = triangleData[2 * 3 + 1] - triangleData[0 * 3 + 1];

		auto normalZ = e1x*e2y - e1y*e2x;
		
		if (normalZ < 0.0f)
		{
			processTriangle(triangleData, frontBufData, glData_.vBuffersFront);
			++numFront;
		}
		else if (normalZ > 0.0f)
		{
			processTriangle(triangleData, backBufData, glData_.vBuffersBack);
			++numBack;
		}
		else
		{
			processTriangle(triangleData, vertBufData, glData_.vBuffersVertical);
			++numVertical;
		}

		//model_.geometry.insert(model_.geometry.end(), triangleData.begin(), triangleData.end());
	}

	updateBuffers(frontBufData, glData_.vBuffersFront, true);
	updateBuffers(backBufData, glData_.vBuffersBack, true);
	updateBuffers(vertBufData, glData_.vBuffersVertical, true);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	std::cout << "Front facing: " << numFront << std::endl;
	std::cout << "Back facing: " << numBack << std::endl;
	std::cout << "Vertical: " << numVertical << std::endl;

	assert(numTriangles == (numFront + numBack + numVertical));

	model_.pos = model_.min.z;
	auto extent = model_.max - model_.min;

	if (extent.x > settings_.plateWidth || extent.y > settings_.plateHeight)
	{
		std::stringstream s;
		s << "Model [" << extent.x << ", " << extent.y << "] is larger than platform ["
			<< settings_.plateWidth << ", " << settings_.plateHeight << "]";
		throw std::runtime_error(s.str());
	}

	std::cout << "Total layers: " << GetLayersCount() << std::endl;
	model_.numFront = numFront;
	model_.numBack = numBack;
	model_.numVertical = numVertical;

	/*struct Key
	{
		uint64_t low;
		uint32_t high;

		bool operator==(const Key& k) const
		{
			return k.low == low && k.high == high;
		}
	};
	auto& KeyHash = [](const Key& k) { return std::hash<decltype(k.high)>()(k.high) ^ std::hash<decltype(k.low)>()(k.low);  };
	std::unordered_map<Key, uint32_t, decltype(KeyHash)> vertMerge(numTriangles, KeyHash);

	std::vector<uint32_t> indexBuffer;
	Key key;
	for (auto i = 0ull, s = model_.geometry.size(); i < s; i += 9)
	{
		key.low = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 0]);
		key.low = key.low << 32 | *reinterpret_cast<uint32_t*>(&model_.geometry[i + 1]);
		key.high = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 2]);
		auto result = vertMerge.insert(std::make_pair(key, static_cast<uint32_t>(vertMerge.size())));
		indexBuffer.push_back(result.first->second);

		key.low = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 3]);
		key.low = key.low << 32 | *reinterpret_cast<uint32_t*>(&model_.geometry[i + 4]);
		key.high = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 5]);
		result = vertMerge.insert(std::make_pair(key, static_cast<uint32_t>(vertMerge.size())));
		indexBuffer.push_back(result.first->second);

		key.low = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 6]);
		key.low = key.low << 32 | *reinterpret_cast<uint32_t*>(&model_.geometry[i + 7]);
		key.high = *reinterpret_cast<uint32_t*>(&model_.geometry[i + 8]);
		result = vertMerge.insert(std::make_pair(key, static_cast<uint32_t>(vertMerge.size())));
		indexBuffer.push_back(result.first->second);
	}
	std::cout << "Merged verts: " << vertMerge.size() << std::endl;

	struct IndexedTri
	{
		uint32_t idx[3];
	};

	IndexedTri * begin = reinterpret_cast<IndexedTri*>(indexBuffer.data());
	IndexedTri * end = begin + numTriangles;
	uint32_t low, high;

	const auto BatchSize = 65530;
	auto sum = 0ull, sum2 = 0ull;
	for (auto i = 0u; i < vertMerge.size(); i += BatchSize)
	{
		low = i;
		high = i + BatchSize;
		auto batch = std::count_if(begin, end, [low, high](const IndexedTri& tri){
			return tri.idx[0] >= low && tri.idx[1] >= low && tri.idx[2] >= low &&
				tri.idx[0] < high && tri.idx[1] < high && tri.idx[2] < high;
		});
		auto batch2 = std::count_if(begin, end, [low, high](const IndexedTri& tri){
			return (tri.idx[0] >= low && tri.idx[1] >= low && tri.idx[0] < high && tri.idx[1] < high) ||
				(tri.idx[1] >= low && tri.idx[2] >= low && tri.idx[1] < high && tri.idx[2] < high) ||
				(tri.idx[0] >= low && tri.idx[2] >= low && tri.idx[0] < high && tri.idx[2] < high);
		});
		sum += batch;
		sum2 += batch2-batch;
	}
	std::cout << "Tris not in batches: " << numTriangles - sum << std::endl;
	std::cout << "Excessive verts to add: " << sum2 << std::endl;*/
}

void Renderer::LoadObj(const std::string& file, std::vector<float>& vb, std::vector<int>& ib)
{
	std::fstream f(file, std::ios::in);
	if (f.fail() || f.bad())
	{
		throw std::runtime_error(strerror(errno));
	}

	std::string type;
	std::string line;
	std::string ind;

	while (!f.eof())
	{
		std::getline(f, line);

		std::istringstream s(line);
		s >> type;
		if (!type.length())
		{
			continue;
		}
		if (type == "v")
		{
			float x, y, z;
			s >> x >> y >> z;
			vb.push_back(x);
			vb.push_back(y);
			vb.push_back(z);
		}
		else if (type == "f")
		{
			s >> ind;
			ib.push_back(std::stoi(ind));
			s >> ind;
			ib.push_back(std::stoi(ind));
			s >> ind;
			ib.push_back(std::stoi(ind));
		}
	}
}

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
				throw std::runtime_error("Only support 8 bit per channel RGB (no alpha) PNG files");
			}

			glBindTexture(GL_TEXTURE_2D, glData_.machineMaskTexture[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixelData[0]);

			GlCheck("Error creating texture " + settings_.machineMaskFile[i]);
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

std::vector<uint8_t> ReadPng(const std::string& fileName, uint32_t& width, uint32_t& height, uint32_t& bitsPerPixel)
{
	FILE* fp = nullptr;
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;

	try
	{
		unsigned char header[8];    // 8 is the maximum size that can be checked

		/* open file and test for it being a png */
		fp = fopen(fileName.c_str(), "rb");
		if (!fp)
			throw std::runtime_error("PNG file could not be opened for reading");

		fread(header, 1, sizeof(header), fp);
		if (png_sig_cmp(header, 0, 8))
			throw std::runtime_error("File is not recognized as a PNG file");


		/* initialize stuff */
		png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

		if (!png_ptr)
			throw std::runtime_error("png_create_read_struct failed");

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr)
			throw std::runtime_error("png_create_info_struct failed");

		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during init_io");

		png_init_io(png_ptr, fp);
		png_set_sig_bytes(png_ptr, 8);

		png_read_info(png_ptr, info_ptr);

		auto png_width = png_get_image_width(png_ptr, info_ptr);
		auto png_height = png_get_image_height(png_ptr, info_ptr);
		auto color_type = png_get_color_type(png_ptr, info_ptr);
		auto channel_depth = png_get_bit_depth(png_ptr, info_ptr);

		/*auto number_of_passes = */png_set_interlace_handling(png_ptr);
		png_read_update_info(png_ptr, info_ptr);

		/* read file */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during read_image");

		auto channels = 0;
		switch (color_type)
		{
		case PNG_COLOR_TYPE_RGB:
			channels = 3;
			break;
		case PNG_COLOR_TYPE_RGBA:
			channels = 4;
			break;
		default:
			throw std::runtime_error("PNG reader: can only read RGB or RGBA files");
		}

		std::vector<uint8_t> data;
		std::vector<uint8_t*> rowPointers;

		const auto pixelRowByteSize = png_width * channel_depth * channels / 8;
		data.resize(pixelRowByteSize * png_height);
		rowPointers.resize(png_height);
		for (auto y = 0u; y < png_height; ++y)
		{
			rowPointers[y] = &data[0] + y * pixelRowByteSize;
		}

		png_read_image(png_ptr, &rowPointers[0]);
		fclose(fp);
		fp = nullptr;

		width = png_width;
		height = png_height;
		bitsPerPixel = channel_depth * channels;
		return data;
	}
	catch(const std::exception&)
	{
		if (fp)
		{
			fclose(fp);
		}

		if (png_ptr || info_ptr)
		{
			png_destroy_write_struct(&png_ptr, &info_ptr);
		}

		throw;
	}
}

void WritePng(const std::string& fileName, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixData)
{
	const auto ChannelBitDepth = 8;

	FILE *fp = nullptr;
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;
	try
	{
		/* create file */
		fp = fopen(fileName.c_str(), "wb");
		if (!fp)
			throw std::runtime_error("Can't create png file");

		/* initialize stuff */
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr)
			throw std::runtime_error("png_create_write_struct failed");

		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr)
			throw std::runtime_error("png_create_info_struct failed");

		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during init_io");

		png_init_io(png_ptr, fp);

		/* write header */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during writing header");

		auto nChannels = pixData.size() / (width * height);
		auto color_type = 0;
		switch (nChannels)
		{
		case 1:
			color_type = PNG_COLOR_TYPE_GRAY;
			break;
		case 3:
			color_type = PNG_COLOR_TYPE_RGB;
			break;
		case 4:
			color_type = PNG_COLOR_TYPE_RGBA;
			break;
		default:
			throw std::runtime_error("Can only write 1, 3 or 4 channel PNG");
		}

		png_set_IHDR(png_ptr, info_ptr, width, height,
					ChannelBitDepth, color_type, PNG_INTERLACE_NONE,
					PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

		png_write_info(png_ptr, info_ptr);


		/* write bytes */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("Error during writing bytes");

		std::vector<const uint8_t*> row_pointers(height);
		for (auto i = 0u; i < height; ++i)
		{
			row_pointers[i] = &pixData[width * nChannels * i];
		}

		png_write_image(png_ptr, const_cast<uint8_t**>(&row_pointers[0]));


		/* end write */
		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("[write_png_file] Error during end of write");

		png_write_end(png_ptr, NULL);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		fp = nullptr;
	}
	catch (const std::exception&)
	{
		if (fp)
		{
			fclose(fp);
		}

		if (png_ptr || info_ptr)
		{
			png_destroy_write_struct(&png_ptr, &info_ptr);
		}

		throw;
	}
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