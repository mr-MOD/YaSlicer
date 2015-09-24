/*
 * Renderer.cpp
 *
 *  Created on: Jul 12, 2015
 *      Author: mod
 */

#include "Renderer.h"
#include "Png.h"
#include "Loaders.h"
#include "Raster.h"

#include <stdexcept>
#include <numeric>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <cerrno>

const auto PNGBytesPerPixel = 1;

const std::string VShader = SHADER
(
	precision mediump float;

	attribute vec3 vPosition;
	uniform mat4 wvp;
	uniform vec2 mirror;
	void main()
	{
		gl_Position = wvp * vec4(vPosition, 1);
		gl_Position.xy = gl_Position.xy * mirror;
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

Renderer::GLData::GLData() :
	mainProgram(0),
	mainVertexPosAttrib(0),
	mainTransformUniform(0),
	mainMirrorUniform(0),

	maskProgram(0),
	maskVertexPosAttrib(0),
	maskWVTransformUniform(0),
	maskWVPTransformUniform(0),
	maskTextureUniform(0),
	maskPlateSizeUniform(0),

#ifdef _MSC_VER
	machineMaskTexture( { 0, 0 } )
#else
	machineMaskTexture{ { 0, 0 } }
#endif
{
}

Renderer::GLData::~GLData()
{
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
}

Renderer::Renderer(const Settings& settings) :
settings_(settings), curMask_(0),
cullFront_(GL_FRONT), cullBack_(GL_BACK),
mirror_(1,1)
{
	if (settings_.offscreen)
	{
		glContext_ = CreateOffscreenGlContext(settings_.renderWidth, settings_.renderHeight, settings_.samples);
	}
	else
	{
		glContext_ = CreateFullscreenGlContext(settings_.renderWidth, settings_.renderHeight, settings_.samples);
	}

	glData_.mainProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, VShader), CreateShader(GL_FRAGMENT_SHADER, FShader));
	glData_.mainTransformUniform = glGetUniformLocation(glData_.mainProgram, "wvp");
	glData_.mainMirrorUniform = glGetUniformLocation(glData_.mainProgram, "mirror");
	glData_.mainVertexPosAttrib = glGetAttribLocation(glData_.mainProgram, "vPosition");
	GlCheck("Error initializing main shader data");

	glData_.maskProgram = CreateProgram(CreateShader(GL_VERTEX_SHADER, MaskVShader), CreateShader(GL_FRAGMENT_SHADER, MaskFShader));
	glData_.maskWVTransformUniform = glGetUniformLocation(glData_.maskProgram, "wv");
	glData_.maskWVPTransformUniform = glGetUniformLocation(glData_.maskProgram, "wvp");
	glData_.maskPlateSizeUniform = glGetUniformLocation(glData_.maskProgram, "plateSize");
	glData_.maskTextureUniform = glGetUniformLocation(glData_.maskProgram, "texture");
	glData_.maskVertexPosAttrib = glGetAttribLocation(glData_.maskProgram, "vPosition");
	GlCheck("Error initializing mask shader data");

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

	CreateGeometryBuffers();
	LoadMasks();
}

Renderer::~Renderer()
{
	for (auto& v : pngSaveResult_)
	{
		v.get();
	}
}

void Renderer::CreateGeometryBuffers()
{
	model_.min.x = model_.min.y = model_.min.z = std::numeric_limits<float>::max();
	model_.max.x = model_.max.y = model_.max.z = std::numeric_limits<float>::lowest();

	LoadModel(settings_.modelFile, true, true, [this](const std::vector<float>& vb, const std::vector<uint16_t>& ib, uint32_t front, uint32_t ortho, uint32_t back) {

		GLData::TriangleData triData(front, ortho, back);

		GLuint buffers[2];
		glGenBuffers(2, buffers);

		glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
		glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(vb[0]), vb.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib.size() * sizeof(ib[0]), ib.data(), GL_STATIC_DRAW);

		this->glData_.vBuffers.push_back(buffers[0]);
		this->glData_.iBuffers.push_back(buffers[1]);
		this->glData_.iCount.push_back(triData);

		for (auto i = 0u; i < vb.size(); i += 3)
		{
			model_.min.x = std::min(model_.min.x, vb[i + 0]);
			model_.min.y = std::min(model_.min.y, vb[i + 1]);
			model_.min.z = std::min(model_.min.z, vb[i + 2]);

			model_.max.x = std::max(model_.max.x, vb[i + 0]);
			model_.max.y = std::max(model_.max.y, vb[i + 1]);
			model_.max.z = std::max(model_.max.z, vb[i + 2]);
		}
	});
	model_.pos = model_.min.z;

	auto extent = model_.max - model_.min;
	if (extent.x > settings_.plateWidth || extent.y > settings_.plateHeight)
	{
		throw std::runtime_error("Model is larger than platform");
	}
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

	if (!settings_.offscreen)
	{
		glContext_->SwapBuffers();
	}
}

void Renderer::White()
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

	if (!settings_.offscreen)
	{
		glContext_->SwapBuffers();
	}
}

void Renderer::SetMask(uint32_t n)
{
	curMask_ = n;
	Render();
}

void Renderer::Render()
{
	if (!settings_.offscreen)
	{
		RenderFullscreen();
	}
	else
	{
		RenderOffscreen();
	}
}

void Renderer::RenderCommon()
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
	GlCheck("Error rendering frame");

	raster_.clear();
	decltype(raster_) rasterDilate;

	if (settings_.doAxialDilate)
	{
		raster_ = glContext_->GetRaster();
		rasterDilate.resize(raster_.size());
		DilateAxial(raster_, rasterDilate, settings_.renderWidth, settings_.renderHeight);
		raster_.swap(rasterDilate);
	}

	auto currentSlice = static_cast<uint32_t>((model_.pos - model_.min.z) / settings_.step + 0.5f);
	if (settings_.doOmniDirectionalDilate && currentSlice % settings_.omniDilateSliceFactor == 0)
	{
		if (raster_.empty())
		{
			raster_ = glContext_->GetRaster();
		}
		
		rasterDilate.resize(raster_.size());
		ScaledDilate(raster_, rasterDilate, settings_.renderWidth, settings_.renderHeight, settings_.omniDilateScale);
		raster_.swap(rasterDilate);
	}

	if (settings_.doBinarize)
	{
		if (raster_.empty())
		{
			raster_ = glContext_->GetRaster();
		}

		Binarize(raster_, settings_.binarizeThreshold);
	}	
}

void Renderer::RenderOffscreen()
{
	RenderCommon();
}

void Renderer::RenderFullscreen()
{
	RenderCommon();

	if (settings_.doAxialDilate || settings_.doOmniDirectionalDilate)
	{
		glContext_->SetRaster(raster_, settings_.renderWidth, settings_.renderHeight);
	}
	glContext_->SwapBuffers();
}

void Renderer::Model(const glm::mat4x4& wvpMatrix)
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearStencil(0x80);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);

	glUseProgram(glData_.mainProgram);
	glUniformMatrix4fv(glData_.mainTransformUniform, 1, GL_FALSE, glm::value_ptr(wvpMatrix));
	glUniform2fv(glData_.mainMirrorUniform, 1, glm::value_ptr(mirror_));

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glCullFace(cullFront_);
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
	glCullFace(cullBack_);
	for (auto i = 0u; i < glData_.vBuffers.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, glData_.vBuffers[i]);
		glVertexAttribPointer(glData_.mainVertexPosAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(glData_.mainVertexPosAttrib);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glData_.iBuffers[i]);
		glDrawElements(GL_TRIANGLES, glData_.iCount[i].frontFacing + glData_.iCount[i].orthoFacing, GL_UNSIGNED_SHORT, nullptr);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GlCheck("Error rendering model");
}

void Renderer::Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix)
{
	glCullFace(GL_BACK);

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

void Renderer::SavePng(const std::string& fileName)
{
	if (raster_.empty())
	{
		raster_ = glContext_->GetRaster();
	}

	auto pixData = std::make_shared<std::vector<uint8_t>>(std::move(raster_));
	auto concurrency = settings_.queue;
	bool runOnMainThread = pngSaveResult_.size() > concurrency;

	const auto targetWidth = settings_.renderWidth;
	const auto targetHeight = settings_.renderHeight;
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

void Renderer::MirrorX()
{
	mirror_.x *= -1;
	std::swap(cullFront_, cullBack_);

	Render();
}

void Renderer::MirrorY()
{
	mirror_.y *= -1;
	std::swap(cullFront_, cullBack_);

	Render();
}

void Renderer::LoadMasks()
{
	for (auto i = 0u; i < settings_.machineMaskFile.size(); ++i)
	{
		if (!settings_.machineMaskFile[i].empty())
		{
			uint32_t width = 0, height = 0, bitsPerPixel = 0;
			auto pixelData = ReadPng(settings_.machineMaskFile[i], width, height, bitsPerPixel);

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