#pragma once
#include "ErrorHandling.h"

#include <cstdint>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

template <typename Strategy>
class GLHandle
{
public:
	static GLHandle Create() { return GLHandle(Strategy::Create()); }

	GLHandle() : handle_(0) {}
	explicit GLHandle(GLuint handle) : handle_(handle) {}
	GLHandle(GLHandle&& other) : handle_(other.handle_)
	{
		other.handle_ = 0;
	}

	GLHandle& operator=(GLHandle&& other)
	{
		Destroy();
		handle_ = other.handle_;
		other.handle_ = 0;
		return *this;
	}

	~GLHandle()
	{
		Destroy();
	}

	GLuint GetHandle() const { return handle_; }
	bool IsValid() const { return handle_ != 0; }

private:
	GLHandle(const GLHandle& other);
	GLHandle& operator=(const GLHandle& other);

	void Destroy()
	{
		if (handle_)
		{
			Strategy::Delete(handle_);
			handle_ = 0;
		}
	}

	GLuint handle_;
};

struct GLBufferStrategy
{
	static GLuint Create() { GLuint handle = 0; glGenBuffers(1, &handle); return handle; }
	static void Delete(GLuint handle) { glDeleteBuffers(1, &handle); }
};

struct GLTextureStrategy
{
	static GLuint Create() { GLuint handle = 0; glGenTextures(1, &handle); return handle; }
	static void Delete(GLuint handle) { glDeleteTextures(1, &handle); }
};

struct GLFramebufferStrategy
{
	static GLuint Create() { GLuint handle = 0; glGenFramebuffers(1, &handle); return handle; }
	static void Delete(GLuint handle) { glDeleteFramebuffers(1, &handle); }
};

struct GLRenderbufferStrategy
{
	static GLuint Create() { GLuint handle = 0; glGenRenderbuffers(1, &handle); return handle; }
	static void Delete(GLuint handle) { glDeleteRenderbuffers(1, &handle); }
};

struct GLFragmentShaderStrategy
{
	static GLuint Create() { return glCreateShader(GL_FRAGMENT_SHADER); }
	static void Delete(GLuint handle) { glDeleteShader(handle); }
};

struct GLVertexShaderStrategy
{
	static GLuint Create() { return glCreateShader(GL_VERTEX_SHADER); }
	static void Delete(GLuint handle) { glDeleteShader(handle); }
};

struct GLProgramStrategy
{
	static GLuint Create() { return glCreateProgram(); }
	static void Delete(GLuint handle) { glDeleteProgram(handle); }
};

typedef GLHandle<GLBufferStrategy> GLBuffer;
typedef GLHandle<GLTextureStrategy> GLTexture;
typedef GLHandle<GLFramebufferStrategy> GLFramebuffer;
typedef GLHandle<GLRenderbufferStrategy> GLRenderbuffer;
typedef GLHandle<GLFragmentShaderStrategy> GLFragmentShader;
typedef GLHandle<GLVertexShaderStrategy> GLVertexShader;
typedef GLHandle<GLProgramStrategy> GLProgram;

struct IGlContext
{
	virtual uint32_t GetSurfaceWidth() const = 0;
	virtual uint32_t GetSurfaceHeight() const = 0;

	virtual std::vector<uint8_t> GetRaster() = 0;
	virtual void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height) = 0;

	virtual void SwapBuffers() = 0;
	virtual void ResetFBO() = 0;

	virtual void CreateTextureFBO(GLFramebuffer& fbo, GLTexture& texture) = 0;
	virtual void Resolve(const GLFramebuffer& fboTo) = 0;

	virtual ~IGlContext() {}
};

class RasterSetter
{
public:
	RasterSetter();

	void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height);
private:

	GLTexture texture_;
	GLProgram program_;

	GLuint texelSizeUniform_;
	GLuint textureUniform_;
	GLuint vertexPosAttrib_;
};

#define SHADER(...) #__VA_ARGS__

GLFragmentShader CreateFragmentShader(const std::string& source);
GLVertexShader CreateVertexShader(const std::string& source);

GLProgram CreateProgram(const GLVertexShader& vertexShader, const GLFragmentShader& fragShader);

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);
std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);

void GlCheck(const std::string& s);

#define GL_CHECK() GlCheck("GlCheck failed at "FILE_LINE)