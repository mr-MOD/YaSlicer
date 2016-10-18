#pragma once

#ifdef ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#ifdef GLEW
#include <GL/glew.h>
#endif

#include <cstdint>
#include <string>

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
	GLHandle(const GLHandle& other) = delete;
	GLHandle& operator=(const GLHandle& other) = delete;

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
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLTextureStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLFramebufferStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLRenderbufferStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLFragmentShaderStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLVertexShaderStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

struct GLProgramStrategy
{
	static GLuint Create();
	static void Delete(GLuint handle);
};

typedef GLHandle<GLBufferStrategy> GLBuffer;
typedef GLHandle<GLTextureStrategy> GLTexture;
typedef GLHandle<GLFramebufferStrategy> GLFramebuffer;
typedef GLHandle<GLRenderbufferStrategy> GLRenderbuffer;
typedef GLHandle<GLFragmentShaderStrategy> GLFragmentShader;
typedef GLHandle<GLVertexShaderStrategy> GLVertexShader;
typedef GLHandle<GLProgramStrategy> GLProgram;

#define SHADER(...) #__VA_ARGS__

inline GLFragmentShader CreateFragmentShader(const std::string& source);
inline GLVertexShader CreateVertexShader(const std::string& source);
inline GLProgram CreateProgram(const GLVertexShader& vertexShader, const GLFragmentShader& fragShader);

inline void GlCheck(const std::string& s);
#define GL_CHECK() GlCheck("GlCheck failed at "FILE_LINE)

inline void CompileShader(GLuint shader, const std::string& source)
{
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

		throw std::runtime_error("Shader compilation error: " + infoLog);
	}
}

inline GLVertexShader CreateVertexShader(const std::string& source)
{
	auto shader = GLVertexShader::Create();
	CompileShader(shader.GetHandle(), source);
	return shader;
}

inline GLFragmentShader CreateFragmentShader(const std::string& source)
{
	auto shader = GLFragmentShader::Create();
	CompileShader(shader.GetHandle(), source);
	return shader;
}

inline GLProgram CreateProgram(const GLVertexShader& vertexShader, const GLFragmentShader& fragShader)
{
	auto program = GLProgram::Create();

	glAttachShader(program.GetHandle(), vertexShader.GetHandle());
	glAttachShader(program.GetHandle(), fragShader.GetHandle());

	glLinkProgram(program.GetHandle());

	GLint status = 0;
	glGetProgramiv(program.GetHandle(), GL_LINK_STATUS, &status);
	if (!status)
	{
		GLsizei length;
		std::string infoLog;
		glGetProgramiv(program.GetHandle(), GL_INFO_LOG_LENGTH, &length);
		infoLog.resize(length);
		glGetProgramInfoLog(program.GetHandle(), static_cast<GLsizei>(infoLog.length()), &length, &infoLog[0]);

		throw std::runtime_error("Program link error: " + infoLog);
	}
	return program;
}

inline std::string glErrorString(GLenum err)
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
#ifdef ANGLE
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
#endif
#ifdef GLEW
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
#endif
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

inline void GlCheck(const std::string& s)
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

inline GLuint GLBufferStrategy::Create() { GLuint handle = 0; glGenBuffers(1, &handle); return handle; }
inline void GLBufferStrategy::Delete(GLuint handle) { glDeleteBuffers(1, &handle); }

inline GLuint GLTextureStrategy::Create() { GLuint handle = 0; glGenTextures(1, &handle); return handle; }
inline void GLTextureStrategy::Delete(GLuint handle) { glDeleteTextures(1, &handle); }

inline GLuint GLFramebufferStrategy::Create() { GLuint handle = 0; glGenFramebuffers(1, &handle); return handle; }
inline void GLFramebufferStrategy::Delete(GLuint handle) { glDeleteFramebuffers(1, &handle); }

inline GLuint GLRenderbufferStrategy::Create() { GLuint handle = 0; glGenRenderbuffers(1, &handle); return handle; }
inline void GLRenderbufferStrategy::Delete(GLuint handle) { glDeleteRenderbuffers(1, &handle); }

inline GLuint GLFragmentShaderStrategy::Create() { return glCreateShader(GL_FRAGMENT_SHADER); }
inline void GLFragmentShaderStrategy::Delete(GLuint handle) { glDeleteShader(handle); }

inline GLuint GLVertexShaderStrategy::Create() { return glCreateShader(GL_VERTEX_SHADER); }
inline void GLVertexShaderStrategy::Delete(GLuint handle) { glDeleteShader(handle); }

inline GLuint GLProgramStrategy::Create() { return glCreateProgram(); }
inline void GLProgramStrategy::Delete(GLuint handle) { glDeleteProgram(handle); }
