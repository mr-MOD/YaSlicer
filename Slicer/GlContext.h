#pragma once
#include "ErrorHandling.h"
#include <GLHelpers.h>

#include <cassert>
#include <memory>
#include <string>
#include <vector>


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

	GLuint textureUniform_;
	GLuint vertexPosAttrib_;
};

std::unique_ptr<IGlContext> CreateFullscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);
std::unique_ptr<IGlContext> CreateOffscreenGlContext(uint32_t width, uint32_t height, uint32_t samples);