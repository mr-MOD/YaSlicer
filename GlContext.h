#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

struct IGlContext
{
	virtual uint32_t GetSurfaceWidth() const = 0;
	virtual uint32_t GetSurfaceHeight() const = 0;

	virtual std::vector<uint8_t> GetRaster() = 0;
	virtual void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height) = 0;

	virtual void SwapBuffers() = 0;

	virtual ~IGlContext() {}
};

class RasterSetter
{
public:
	RasterSetter();

	void SetRaster(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height);
private:
	struct GlData
	{
		GlData();
		~GlData();

		GLuint texture;
		GLuint program;

		GLuint texelSizeUniform;
		GLuint textureUniform;
		GLuint vertexPosAttrib;
	};

	GlData gl_;
};

#define SHADER(...) #__VA_ARGS__

GLuint CreateShader(GLenum type, const std::string& source);
GLuint CreateProgram(GLuint vertexShader, GLuint fragShader);

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);
std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);

void GlCheck(const std::string& s);