/*
 * Renderer.cpp
 *
 *  Created on: Jul 12, 2015
 *      Author: mod
 */

#include "Renderer.h"
#include "Shaders.h"

#include <PngFile.h>
#include <Loaders.h>
#include <Raster.h>

#include <stdexcept>
#include <numeric>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <cerrno>

#include <boost/filesystem.hpp>
#include <boost/scope_exit.hpp>

namespace
{
	bool HasOverhangs(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height);
}


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

palette_(CreateGrayscalePalette())
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
	ASSERT(mainTransformUniform_ != -1);
	mainMirrorUniform_ = glGetUniformLocation(mainProgram_.GetHandle(), "mirror");
	ASSERT(mainMirrorUniform_ != -1);
	mainVertexPosAttrib_ = glGetAttribLocation(mainProgram_.GetHandle(), "vPosition");
	ASSERT(mainVertexPosAttrib_ != -1);
	GL_CHECK();

	maskProgram_ = CreateProgram(CreateVertexShader(MaskVShader), CreateFragmentShader(MaskFShader));
	maskWVTransformUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "wv");
	ASSERT(maskWVTransformUniform_ != -1);
	maskWVPTransformUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "wvp");
	ASSERT(maskWVPTransformUniform_ != -1);
	maskPlateSizeUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "plateSize");
	ASSERT(maskPlateSizeUniform_ != -1);
	maskTextureUniform_ = glGetUniformLocation(maskProgram_.GetHandle(), "maskTexture");
	ASSERT(maskTextureUniform_ != -1);
	maskVertexPosAttrib_ = glGetAttribLocation(maskProgram_.GetHandle(), "vPosition");
	ASSERT(maskVertexPosAttrib_ != -1);
	GL_CHECK();

	axialDilateProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(AxialDilateFShader));
	omniDilateProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(OmniDilateFShader));
	binarizeProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(BinarizeFShader));
	differenceProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(DifferenceFShader));
	
	whiteTexture_ = GLTexture::Create();

	glContext_->CreateTextureFBO(imageFBO_, imageTexture_);
	glContext_->CreateTextureFBO(previousLayerImageFBO_, previousLayerImageTexture_);
	White();
	glContext_->Resolve(previousLayerImageFBO_);
	glContext_->CreateTextureFBO(differenceFBO_, differenceTexture_);
	GL_CHECK();

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
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(GL_TRUE);

	CreateGeometryBuffers();
	LoadMasks();

	if (settings_.mirrorX)
	{
		mirror_.x *= -1;
		std::swap(cullFront_, cullBack_);
	}

	if (settings_.mirrorY)
	{
		mirror_.y *= -1;
		std::swap(cullFront_, cullBack_);
	}
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
	model_.pos = model_.min.z + settings_.step/2;
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
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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

	auto offsetX = (settings_.plateWidth / settings_.renderWidth) * settings_.modelOffset.x;
	auto offsetY = (settings_.plateHeight / settings_.renderHeight) * settings_.modelOffset.y;

	auto model = glm::scale(glm::vec3(1.0f, 1.0f, 1.0f)) * glm::translate(offsetX, offsetY, 0.0f);

	auto view = glm::lookAt(glm::vec3(middle.x, middle.y, model_.pos),
		glm::vec3(middle.x, middle.y, model_.max.z + 1.0f),
		glm::vec3(0, -1.0f, 0));
	auto proj = glm::ortho(-settings_.plateHeight * 0.5f * aspect, settings_.plateHeight * 0.5f * aspect,
		-settings_.plateHeight * 0.5f, settings_.plateHeight * 0.5f,
		0.0f, extent.z);

	auto wvMatrix = view * model;
	auto wvpMatrix = proj * view * model;

	GL_CHECK();

	Model(wvpMatrix);
	Mask(wvpMatrix, wvMatrix);

	if (settings_.doAxialDilate)
	{
		glContext_->Resolve(imageFBO_);
		RenderAxialDilate();
	}

	auto currentSlice = GetCurrentSlice();
	if (settings_.doOmniDirectionalDilate && currentSlice % settings_.omniDilateSliceFactor == 0)
	{
		const auto KernelSize = 3;
		glContext_->Resolve(imageFBO_);
		RenderOmniDilate(settings_.omniDilateScale, KernelSize);
	}

	if (settings_.doBinarize)
	{
		glContext_->Resolve(imageFBO_);
		RenderBinarize(settings_.binarizeThreshold);
	}
}

void Renderer::RenderOffscreen()
{
	RenderCommon();
}

void Renderer::RenderFullscreen()
{
	RenderCommon();
	glContext_->SwapBuffers();
}

void Renderer::Model(const glm::mat4x4& wvpMatrix)
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearStencil(0x80);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniform1i(maskTextureUniform_, 0);
	glDrawArrays(GL_TRIANGLES, 0, _countof(quad) / 3);

	GL_CHECK();
}

uint32_t Renderer::GetCurrentSlice() const
{
	return static_cast<uint32_t>(std::max((model_.pos - model_.min.z) / settings_.step + 0.5f - 1, 0.0f));
}

uint32_t Renderer::GetCurrentSliceERM() const
{
	return GetCurrentSlice() * (settings_.enableERM ? 2 : 1);
}

void Renderer::RenderAxialDilate()
{
	Render2DFilter(axialDilateProgram_);
}

void Renderer::RenderOmniDilate(float scale, uint32_t kernelSize)
{
	UniformSetters omniUniforms
	{
		[scale, kernelSize](const GLProgram& program)
		{
			const auto scaleUniform = glGetUniformLocation(program.GetHandle(), "scale");
			ASSERT(scaleUniform != -1);
			glUniform1f(scaleUniform, scale);

			const auto kernelSizeUniform = glGetUniformLocation(program.GetHandle(), "kernelSize");
			ASSERT(kernelSizeUniform != -1);
			glUniform1f(kernelSizeUniform, static_cast<float>(kernelSize));
		}
	};
	Render2DFilter(omniDilateProgram_, omniUniforms);
}

void Renderer::RenderBinarize(uint32_t threshold)
{
	UniformSetters binarizeUniforms
	{
		[threshold](const GLProgram& program)
		{
			const auto thresholdUniform = glGetUniformLocation(program.GetHandle(), "threshold");
			ASSERT(thresholdUniform != -1);
			glUniform1f(thresholdUniform, std::max(1.0f, threshold / 255.0f));
		}
	};
	Render2DFilter(binarizeProgram_, binarizeUniforms);
}

void Renderer::RenderDifference()
{
	UniformSetters differenceUniforms
	{
		[this](const GLProgram& program)
		{
			const auto previousLayerTextureUniform = glGetUniformLocation(program.GetHandle(), "previousLayerTexture");
			ASSERT(previousLayerTextureUniform != -1);
			glUniform1i(previousLayerTextureUniform, 1);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, this->previousLayerImageTexture_.GetHandle());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	};
	Render2DFilter(differenceProgram_, differenceUniforms);
}

void Renderer::Render2DFilter(const GLProgram& program, const UniformSetters& additionalUniformSetters)
{
	glViewport(0, 0, settings_.renderWidth, settings_.renderHeight);

	glDisable(GL_STENCIL_TEST);
	glCullFace(GL_FRONT);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glUseProgram(program.GetHandle());
	const auto textureUniform = glGetUniformLocation(program.GetHandle(), "texture");
	ASSERT(textureUniform != -1);
	const auto texelSizeUniform = glGetUniformLocation(program.GetHandle(), "texelSize");
	const auto vertexPosAttrib = glGetAttribLocation(program.GetHandle(), "vPosition");
	ASSERT(vertexPosAttrib != -1);
	GL_CHECK();

	for (const auto& s : additionalUniformSetters)
	{
		s(program);
	}
	GL_CHECK();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, imageTexture_.GetHandle());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glUniform1i(textureUniform, 0);
	glUniform2f(texelSizeUniform, 1.0f / settings_.renderWidth, 1.0f / settings_.renderHeight);

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
	glVertexAttribPointer(vertexPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(vertexPosAttrib);
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
	auto future = std::async([pixData, fileName, targetWidth, targetHeight, this]() {
		const auto BitsPerChannel = 8;
		WritePng(fileName, targetWidth, targetHeight, BitsPerChannel, *pixData, this->palette_);
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

void Renderer::ERM()
{
	glm::vec2 offset(0.5f, 0.5f);
	settings_.modelOffset -= offset;

	BOOST_SCOPE_EXIT(&settings_, &offset)
	{
		settings_.modelOffset += offset;
	}
	BOOST_SCOPE_EXIT_END

	Render();
}

void Renderer::AnalyzeOverhangs()
{
	glContext_->Resolve(imageFBO_);
	glBindFramebuffer(GL_FRAMEBUFFER, differenceFBO_.GetHandle());
	RenderDifference();
	raster_ = glContext_->GetRaster();
	if (HasOverhangs(raster_, glContext_->GetSurfaceWidth(), glContext_->GetSurfaceHeight()))
	{
		std::cout << "Has overhangs at layer: " << GetCurrentSliceERM() << "\n";
		std::stringstream s;
		s << std::setfill('0') << std::setw(5) << GetCurrentSliceERM() << "_overhangs.png";
		SavePng((boost::filesystem::path(settings_.outputDir) / s.str()).string());
	}
	raster_.clear();

	glBindFramebuffer(GL_FRAMEBUFFER, previousLayerImageFBO_.GetHandle());
	const auto supportedPixels = static_cast<uint32_t>(ceil(settings_.maxSupportedDistance * settings_.renderWidth / settings_.plateWidth));
	RenderOmniDilate(1.0f, supportedPixels * 2 + 1);
	glContext_->ResetFBO();
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

namespace
{

	bool HasOverhangs(const std::vector<uint8_t>& raster, uint32_t width, uint32_t height)
	{
		const auto Threshold = 255;
		for (auto v : raster)
		{
			if (v >= Threshold)
			{
				return true;
			}
		}
		return false;
	}

}
