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
#include <Geometry.h>

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
} //namespace


Renderer::Renderer(const Settings& settings) :
settings_(settings),
mirror_(1,1),
modelOffset_(0,0),

mainVertexPosAttrib_(0),
mainVertexNormalAttrib_(0),
mainTransformUniform_(0),
mainMirrorUniform_(0),
mainInflateUniform_(0),

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
	mainInflateUniform_ = glGetUniformLocation(mainProgram_.GetHandle(), "inflate");
	ASSERT(mainInflateUniform_ != -1);
	mainVertexPosAttrib_ = glGetAttribLocation(mainProgram_.GetHandle(), "vPosition");
	ASSERT(mainVertexPosAttrib_ != -1);
	mainVertexNormalAttrib_ = glGetAttribLocation(mainProgram_.GetHandle(), "vNormal");
	ASSERT(mainVertexNormalAttrib_ != -1);
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

	omniDilateProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(OmniDilateFShader));
	differenceProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(DifferenceFShader));
	combineMaxProgram_ = CreateProgram(CreateVertexShader(Filter2DVShader), CreateFragmentShader(CombineMaxFShader));
	
	whiteTexture_ = GLTexture::Create();
	maskTexture_ = GLTexture::Create();

	glContext_->CreateTextureFBO(imageFBO_, imageTexture_);
	glContext_->CreateTextureFBO(previousLayerImageFBO_, previousLayerImageTexture_);
	White();
	glContext_->Resolve(previousLayerImageFBO_);
	glContext_->CreateTextureFBO(temporaryFBO_, temporaryTexture_);
	GL_CHECK();

	const uint32_t WhiteOpaquePixel = 0xFFFFFFFF;
	glBindTexture(GL_TEXTURE_2D, whiteTexture_.GetHandle());
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &WhiteOpaquePixel);

	GL_CHECK();
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(GL_TRUE);

	CreateGeometryBuffers();

	if (settings_.mirrorX)
	{
		mirror_.x *= -1;
	}

	if (settings_.mirrorY)
	{
		mirror_.y *= -1;
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

	LoadModel(settings_.modelFile,
		[this](const std::vector<float>& vb, const std::vector<float>& nb, const std::vector<uint16_t>& ib) {

		auto vertexBuffer = GLBuffer::Create();
		auto normalBuffer = GLBuffer::Create();
		auto indexBuffer = GLBuffer::Create();

		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer.GetHandle());
		glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(vb[0]), vb.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, normalBuffer.GetHandle());
		glBufferData(GL_ARRAY_BUFFER, nb.size() * sizeof(nb[0]), nb.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer.GetHandle());
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib.size() * sizeof(ib[0]), ib.data(), GL_STATIC_DRAW);

		this->vBuffers_.push_back(std::move(vertexBuffer));
		this->nBuffers_.push_back(std::move(normalBuffer));
		this->iBuffers_.push_back(std::move(indexBuffer));
		this->triCount_.push_back(static_cast<GLsizei>(ib.size()));

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

	auto offsetX = (settings_.plateWidth / settings_.renderWidth) * modelOffset_.x;
	auto offsetY = (settings_.plateHeight / settings_.renderHeight) * modelOffset_.y;

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

	Model(wvpMatrix, settings_.doInflate ? settings_.inflateDistance : 0.0f);
	Mask(wvpMatrix, wvMatrix, whiteTexture_);

	if (settings_.doSmallSpotsProcessing)
	{
		glContext_->Resolve(imageFBO_);
		auto raster = glContext_->GetRaster();
		std::vector<uint32_t> out(raster.size());
		std::vector<Segment> segments;
		
		Segmentize(raster, out, segments, settings_.renderWidth, settings_.renderHeight);

		const auto physWidth = settings_.plateWidth / settings_.renderWidth;
		const auto physHeight = settings_.plateHeight / settings_.renderHeight;
		const auto physPixelSquare = physWidth * physHeight;

		for (const auto& v : segments)
		{
			const uint8_t fillValue = v.count*physPixelSquare > settings_.smallSpotThreshold ? 0 : 255;
			
			for (auto y = v.yBegin; y < v.yEnd; ++y)
			{
				for (auto x = v.xBegin; x < v.xEnd; ++x)
				{
					const size_t pixelIndex = y*settings_.renderWidth + x;
					if (out[pixelIndex] == v.val)
					{
						raster[pixelIndex] = fillValue;
					}
				}
			}
		}

		std::vector<uint8_t> rasterDilated(raster.size());
		float expansionSize = 0.0f;
		while (expansionSize <= settings_.smallSpotInflateDistance)
		{
			Dilate(raster, rasterDilated, settings_.renderWidth, settings_.renderHeight);
			std::swap(raster, rasterDilated);
			expansionSize += (physWidth + physHeight) / 2;
		}

		glBindTexture(GL_TEXTURE_2D, maskTexture_.GetHandle());
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, settings_.renderWidth, settings_.renderHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, raster.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		Model(wvpMatrix, (settings_.doInflate ? settings_.inflateDistance : 0.0f)+ settings_.smallSpotInflateDistance);
		Mask(wvpMatrix, wvMatrix, maskTexture_);
		glContext_->Resolve(temporaryFBO_);

		RenderCombineMax(temporaryTexture_);
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

void Renderer::Model(const glm::mat4x4& wvpMatrix, float inflateDistance)
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
	glUniform1f(mainInflateUniform_, inflateDistance);

	glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_INCR);
	glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_DECR);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	for (auto i = 0u; i < vBuffers_.size(); ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vBuffers_[i].GetHandle());
		glVertexAttribPointer(mainVertexPosAttrib_, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(mainVertexPosAttrib_);

		glBindBuffer(GL_ARRAY_BUFFER, nBuffers_[i].GetHandle());
		glVertexAttribPointer(mainVertexNormalAttrib_, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(mainVertexNormalAttrib_);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBuffers_[i].GetHandle());
		glDrawElements(GL_TRIANGLES, triCount_[i], GL_UNSIGNED_SHORT, 0);
	}	

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK();
}

void Renderer::Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix, const GLTexture& mask)
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
	glStencilFunc(settings_.mirrorX ^ settings_.mirrorY ? GL_GREATER : GL_LESS, 0x80, 0xFF);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glUniformMatrix4fv(maskWVTransformUniform_, 1, GL_FALSE, glm::value_ptr(wvMatrix));
	glUniformMatrix4fv(maskWVPTransformUniform_, 1, GL_FALSE, glm::value_ptr(wvpMatrix));
	glUniform2f(maskPlateSizeUniform_, settings_.plateWidth, settings_.plateHeight);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mask.GetHandle());
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

void Renderer::RenderCombineMax(const GLTexture& combineTexture)
{
	UniformSetters combineMaxUniforms
	{
		[this, &combineTexture](const GLProgram& program)
		{
			const auto combineTextureUniform = glGetUniformLocation(program.GetHandle(), "combineTexture");
			ASSERT(combineTextureUniform != -1);
			glUniform1i(combineTextureUniform, 1);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, combineTexture.GetHandle());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	};
	Render2DFilter(combineMaxProgram_, combineMaxUniforms);
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

	Render();
}

void Renderer::MirrorY()
{
	mirror_.y *= -1;

	Render();
}

void Renderer::ERM()
{
	glm::vec2 offset(0.5f, 0.5f);
	modelOffset_ -= offset;

	BOOST_SCOPE_EXIT(&offset, &modelOffset_)
	{
		modelOffset_ += offset;
	}
	BOOST_SCOPE_EXIT_END

	Render();
}

void Renderer::AnalyzeOverhangs(uint32_t imageNumber)
{
	glContext_->Resolve(imageFBO_);
	glBindFramebuffer(GL_FRAMEBUFFER, temporaryFBO_.GetHandle());
	RenderDifference();
	raster_ = glContext_->GetRaster();
	if (HasOverhangs(raster_, glContext_->GetSurfaceWidth(), glContext_->GetSurfaceHeight()))
	{
		std::cout << "Has overhangs at image: " << imageNumber << "\n";
		std::stringstream s;
		s << std::setfill('0') << std::setw(5) << imageNumber << "_overhangs.png";
		SavePng((boost::filesystem::path(settings_.outputDir) / s.str()).string());
	}
	raster_.clear();

	glBindFramebuffer(GL_FRAMEBUFFER, previousLayerImageFBO_.GetHandle());
	const auto supportedPixels = static_cast<uint32_t>(ceil(settings_.maxSupportedDistance * settings_.renderWidth / settings_.plateWidth));
	RenderOmniDilate(1.0f, supportedPixels * 2 + 1);
	glContext_->ResetFBO();
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

	
} //namespace
