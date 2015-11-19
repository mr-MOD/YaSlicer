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
#include "GlContext.h"

#include <stdexcept>
#include <numeric>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <cerrno>

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

//////////////////////////////////////

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
	uniform sampler2D maskTexture;

	void main()
	{
		gl_FragColor = texture2D(maskTexture, texCoord);
	}
);

//////////////////////////////////////

const std::string CombineVShader = SHADER
(
	precision mediump float;
	uniform vec2 texelSize;
	attribute vec2 vPosition;
	varying vec2 normalTexCoord;
	varying vec2 mirrorTexCoord;
	
	void main()
	{
		gl_Position = vec4(vPosition, 0, 1);
		vec2 baseTexCoord = (vPosition + vec2(1, 1)) * 0.5;
		normalTexCoord = baseTexCoord;
		mirrorTexCoord = (vec2(1,1) - baseTexCoord);
	}
);

const std::string CombineFShader = SHADER
(
	precision mediump float;

	varying vec2 normalTexCoord;
	varying vec2 mirrorTexCoord;
	uniform sampler2D normalTexture;
	uniform sampler2D mirrorTexture;

	void main()
	{
		vec4 normal = texture2D(normalTexture, normalTexCoord);
		vec4 mirror = texture2D(mirrorTexture, mirrorTexCoord);
		gl_FragColor = normal + mirror;
	}
);


Renderer::Renderer(const Settings& settings) :
settings_(settings), curMask_(0),
cullFront_(GL_FRONT), cullBack_(GL_BACK),
mirror_(1,1),

mainVertexPosAttrib_(0),
mainTransformUniform_(0),
mainMirrorUniform_(0),

maskVertexPosAttrib_(0),
maskWVTransformUniform_(0),
maskWVPTransformUniform_(0),
maskTextureUniform_(0),
maskPlateSizeUniform_(0),

combineVertexPosAttrib_(0),
combineNormalTextureUniform_(0),
combineMirrorTextureUniform_(0),
combineTexelSizeUniform_(0),

whiteTexture_(GLTexture::Create())
{
	if (settings_.offscreen)
	{
		glContext_ = CreateOffscreenGlContext(settings_.renderWidth, settings_.renderHeight, settings_.samples);
	}
	else
	{
		glContext_ = CreateFullscreenGlContext(settings_.renderWidth, settings_.renderHeight, settings_.samples);
	}

	mainProgram_ = CreateProgram(CreateVertexShader(VShader), CreateFragmentShader(FShader));
	mainTransformUniform_ = glGetUniformLocation(mainProgram_.GetHandle(), "wvp");
	mainMirrorUniform_ = glGetUniformLocation(mainProgram_.GetHandle(), "mirror");
	mainVertexPosAttrib_ = glGetAttribLocation(mainProgram_.GetHandle(), "vPosition");
	GL_CHECK();

	maskProgram_ = CreateProgram(CreateVertexShader(MaskVShader), CreateFragmentShader(MaskFShader));
	maskWVTransformUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "wv");
	maskWVPTransformUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "wvp");
	maskPlateSizeUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "plateSize");
	maskTextureUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "maskTexture");
	maskVertexPosAttrib_ = glGetAttribLocation(maskProgram_.GetHandle(), "vPosition");
	GL_CHECK();

	combineProgram_ = CreateProgram(CreateVertexShader(CombineVShader), CreateFragmentShader(CombineFShader));
	combineNormalTextureUniform_ = glGetUniformLocation(combineProgram_.GetHandle(), "normalTexture");
	combineMirrorTextureUniform_ = glGetUniformLocation(combineProgram_.GetHandle(), "mirrorTexture");
	combineTexelSizeUniform_ = glGetUniformLocation(combineProgram_.GetHandle(), "texelSize");
	combineVertexPosAttrib_ = glGetAttribLocation(combineProgram_.GetHandle(), "vPosition");
	GL_CHECK();

	glContext_->CreateTextureFBO(mirrorImageFBO_, mirrorImageTexture_);
	glContext_->CreateTextureFBO(normalImageFBO_, normalImageTexture_);

	const uint32_t WhiteOpaquePixel = 0xFFFFFFFF;
	glBindTexture(GL_TEXTURE_2D, whiteTexture_.GetHandle());
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &WhiteOpaquePixel);

	for (auto& tex : machineMaskTexture_)
	{
		tex = GLTexture::Create();
		glBindTexture(GL_TEXTURE_2D, tex.GetHandle());
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &WhiteOpaquePixel);
	}
	GL_CHECK();
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

	LoadModel(settings_.modelFile, true, false, [this](const std::vector<float>& vb, const std::vector<uint16_t>& ib, uint32_t front, uint32_t ortho, uint32_t back) {

		TriangleData triData(front, ortho, back);

		auto vertexBuffer = GLBuffer::Create();
		auto indexBuffer = GLBuffer::Create();

		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer.GetHandle());
		glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(vb[0]), vb.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer.GetHandle());
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib.size() * sizeof(ib[0]), ib.data(), GL_STATIC_DRAW);

		this->vBuffers_.push_back(std::move(vertexBuffer));
		this->iBuffers_.push_back(std::move(indexBuffer));
		this->iCount_.push_back(triData);

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

	auto offsetX = -(settings_.plateWidth / settings_.renderWidth) * settings_.modelOffset.x;
	auto offsetY = (settings_.plateHeight / settings_.renderHeight) * settings_.modelOffset.y;

	auto model = glm::scale(glm::vec3(1.0f, 1.0f, 1.0f)) * glm::translate(offsetX, offsetY, 0.0f);

	auto view = glm::lookAt(glm::vec3(middle.x, middle.y, model_.pos),
		glm::vec3(middle.x, middle.y, model_.max.z + 1.0f),
		glm::vec3(0, 1.0f, 0));
	auto proj = glm::ortho(-settings_.plateHeight * 0.5f * aspect, settings_.plateHeight * 0.5f * aspect,
		-settings_.plateHeight * 0.5f, settings_.plateHeight * 0.5f,
		0.0f, extent.z);

	auto wvMatrix = view * model;
	auto wvpMatrix = proj * view * model;

	GL_CHECK();

	mirror_.x *= -1;
	mirror_.y *= -1;
	Model(wvpMatrix);
	Mask(wvpMatrix, wvMatrix);
	glContext_->Resolve(mirrorImageFBO_);

	mirror_.x *= -1;
	mirror_.y *= -1;
	Model(wvpMatrix);
	Mask(wvpMatrix, wvMatrix);
	glContext_->Resolve(normalImageFBO_);

	Combine();

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

	glUseProgram(mainProgram_.GetHandle());
	glUniformMatrix4fv(mainTransformUniform_, 1, GL_FALSE, glm::value_ptr(wvpMatrix));
	glUniform2fv(mainMirrorUniform_, 1, glm::value_ptr(mirror_));

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glCullFace(cullFront_);
	for (auto i = 0u; i < vBuffers_.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vBuffers_[i].GetHandle());
		glVertexAttribPointer(mainVertexPosAttrib_, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(mainVertexPosAttrib_);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,iBuffers_[i].GetHandle());
		glDrawElements(GL_TRIANGLES, iCount_[i].orthoFacing + iCount_[i].backFacing,
			GL_UNSIGNED_SHORT, reinterpret_cast<void*>(iCount_[i].frontFacing * sizeof(uint16_t)));
	}	

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
	glCullFace(cullBack_);
	for (auto i = 0u; i < vBuffers_.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vBuffers_[i].GetHandle());
		glVertexAttribPointer(mainVertexPosAttrib_, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(mainVertexPosAttrib_);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBuffers_[i].GetHandle());
		glDrawElements(GL_TRIANGLES, iCount_[i].frontFacing + iCount_[i].orthoFacing, GL_UNSIGNED_SHORT, nullptr);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK();
}

void Renderer::Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix)
{
	glCullFace(GL_BACK);

	glUseProgram(maskProgram_.GetHandle());
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
	glVertexAttribPointer(maskVertexPosAttrib_, 3, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(maskVertexPosAttrib_);

	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_LESS, 0x80, 0xFF);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glUniformMatrix4fv(maskWVTransformUniform_, 1, GL_FALSE, glm::value_ptr(wvMatrix));
	glUniformMatrix4fv(maskWVPTransformUniform_, 1, GL_FALSE, glm::value_ptr(wvpMatrix));
	glUniform2f(maskPlateSizeUniform_, settings_.plateWidth, settings_.plateHeight);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, machineMaskTexture_[curMask_].GetHandle());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniform1i(maskTextureUniform_, 0);
	glDrawArrays(GL_TRIANGLES, 0, _countof(quad) / 3);

	GL_CHECK();
}

void Renderer::Combine()
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glCullFace(GL_FRONT);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(combineProgram_.GetHandle());

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, normalImageTexture_.GetHandle());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mirrorImageTexture_.GetHandle());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniform1i(combineNormalTextureUniform_, 0);
	glUniform1i(combineMirrorTextureUniform_, 1);

	glUniform2f(combineTexelSizeUniform_, 1.0f / settings_.renderWidth, 1.0f / settings_.renderHeight);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	const float quad[] =
	{
		-1, -1,
		-1, 1,
		1, 1,

		-1, -1,
		1, 1,
		1, -1
	};
	glVertexAttribPointer(combineVertexPosAttrib_, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(combineVertexPosAttrib_);
	glDrawArrays(GL_TRIANGLES, 0, _countof(quad) / 2);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK();
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

			glBindTexture(GL_TEXTURE_2D, machineMaskTexture_[i].GetHandle());
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixelData[0]);

			GL_CHECK();
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}