#include "GlContext.h"

const std::string FullScreenVS = SHADER
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

const std::string FullScreenFS = SHADER
(
	precision mediump float;

	varying vec2 texCoord;
	uniform sampler2D texture;

	void main()
	{
		gl_FragColor = texture2D(texture, texCoord);
	}
);

RasterSetter::RasterSetter() :
	texture_(GLTexture::Create()),
	program_(CreateProgram(CreateVertexShader(FullScreenVS), CreateFragmentShader(FullScreenFS))),
	texelSizeUniform_(0), textureUniform_(0), vertexPosAttrib_(0)
{
	texelSizeUniform_ = glGetUniformLocation(program_.GetHandle(), "texelSize");
	ASSERT(texelSizeUniform_ != -1);
	textureUniform_ = glGetUniformLocation(program_.GetHandle(), "texture");
	ASSERT(textureUniform_ != -1);
	vertexPosAttrib_ = glGetAttribLocation(program_.GetHandle(), "vPosition");
	ASSERT(vertexPosAttrib_ != -1);

	GL_CHECK();
}

void RasterSetter::SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height)
{
	glBindTexture(GL_TEXTURE_2D, texture_.GetHandle());
	if (raster.size() == width*height)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, raster.data());
	}
	else if (raster.size() == width*height * 3)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, raster.data());
	}
	else if (raster.size() == width*height * 4)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, raster.data());
	}
	else
	{
		throw std::runtime_error("SetRaster: Invalid raster size");
	}

	glUseProgram(program_.GetHandle());
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
	glVertexAttribPointer(vertexPosAttrib_, 3, GL_FLOAT, GL_FALSE, 0, quad);

	glCullFace(GL_FRONT);
	glDisable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 0, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glUniform2f(texelSizeUniform_, 1.0f / width, 1.0f / height);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture_.GetHandle());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glUniform1i(textureUniform_, 0);
	glDrawArrays(GL_TRIANGLES, 0, sizeof(quad) / sizeof(quad[0]) / 3);

	GL_CHECK();
}

void CompileShader(GLuint shader, const std::string& source)
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

GLVertexShader CreateVertexShader(const std::string& source)
{
	auto shader = GLVertexShader::Create();
	CompileShader(shader.GetHandle(), source);
	return shader;
}

GLFragmentShader CreateFragmentShader(const std::string& source)
{
	auto shader = GLFragmentShader::Create();
	CompileShader(shader.GetHandle(), source);
	return shader;
}

GLProgram CreateProgram(const GLVertexShader& vertexShader, const GLFragmentShader& fragShader)
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