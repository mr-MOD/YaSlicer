#include "GlContext.h"

const std::string FullScreenVS = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	varying vec2 texCoord;
	void main()
	{
		gl_Position = vec4(vPosition, 1);
		texCoord = (vPosition.xy + vec2(1, 1) ) * 0.5;
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
	textureUniform_(0), vertexPosAttrib_(0)
{
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

