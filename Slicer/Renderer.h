#pragma once

#include "GlContext.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <string>
#include <vector>
#include <array>
#include <future>

#include <cstdint>

#undef min
#undef max

struct Settings
{
	bool offscreen = true;
	std::string modelFile;

	std::string outputDir;

	float step = 0.025f;

	uint32_t renderWidth = 1920;
	uint32_t renderHeight = 1080;

	uint32_t samples = 0;
	uint32_t queue = std::max(1u, std::thread::hardware_concurrency());
	uint32_t whiteLayers = 1;
	float basementBorder = 5.0f;

	float plateWidth = 96.0f;
	float plateHeight = 54.0f;

	bool doInflate = false;
	float inflateDistance = 0.1f;

	bool doOverhangAnalysis = false;
	float maxSupportedDistance = 0.5f;

	bool enableERM = false;
	std::string envisiontechTemplatesPath = "envisiontech";
	
	bool doSmallSpotsProcessing = false;
	float smallSpotThreshold = 1.0f;
	float smallSpotInflateDistance = 0.1f;

	bool mirrorX = false;
	bool mirrorY = false;

	bool simulate = false;
};

class Renderer
{
public:
	Renderer(const Settings& settings);
	~Renderer();

	void SavePng(const std::string& fileName);

	uint32_t GetLayersCount() const;
	void FirstSlice();
	bool NextSlice();
	void White();
	void ERM();
	void AnalyzeOverhangs(uint32_t imageNumber);
	std::pair<glm::vec2, glm::vec2> GetModelProjectionRect() const;

private:
	struct ModelData
	{
		ModelData() : pos()
		{
		}

		float pos;
		glm::vec3 min;
		glm::vec3 max;
	};

	struct MeshInfo
	{
		GLsizei idxCount = 0;
		float zMin = 0.0f;
		float zMax = 0.0f;
	};

	using UniformSetterType = std::function<void(const GLProgram&)>;
	using UniformSetters = std::vector<UniformSetterType>;

	void CreateGeometryBuffers();

	bool IsUpsideDownRendering() const;
	bool ShouldRender(const MeshInfo& info, float inflateDistance);
	void Render();
	glm::mat4x4 CalculateModelTransform() const;
	glm::mat4x4 CalculateViewTransform() const;
	glm::mat4x4 CalculateProjectionTransform() const;
	void RenderCommon();
	void RenderOmniDilate(float scale, uint32_t kernelSize);
	void RenderDifference();
	void RenderCombineMax(const GLTexture& additionalTexture);
	void Render2DFilter(const GLProgram& program, const UniformSetters& additionalUniformSetters = UniformSetters());
	void RenderOffscreen();
	void RenderFullscreen();

	void Model(const glm::mat4x4& wvpMatrix, float inflateDistance);
	void Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix, const GLTexture& mask);

	uint32_t GetCurrentSlice() const;
	float GetMirrorXFactor() const;
	float GetMirrorYFactor() const;
	bool ShouldMirrorX() const;
	bool ShouldMirrorY() const;

	GLProgram mainProgram_;
	GLuint mainVertexPosAttrib_;
	GLuint mainVertexNormalAttrib_;
	GLuint mainTransformUniform_;
	GLuint mainMirrorUniform_;
	GLuint mainInflateUniform_;

	GLProgram maskProgram_;
	GLuint maskVertexPosAttrib_;
	GLuint maskWVPTransformUniform_;
	GLuint maskWVTransformUniform_;
	GLuint maskTextureUniform_;
	GLuint maskPlateSizeUniform_;

	GLProgram omniDilateProgram_;
	GLProgram differenceProgram_;
	GLProgram combineMaxProgram_;

	GLTexture maskTexture_;
	GLTexture whiteTexture_;

	GLFramebuffer imageFBO_;
	GLTexture imageTexture_;

	GLFramebuffer previousLayerImageFBO_;
	GLTexture previousLayerImageTexture_;

	GLFramebuffer temporaryFBO_;
	GLTexture temporaryTexture_;

	std::vector<GLBuffer> vBuffers_;
	std::vector<GLBuffer> nBuffers_;
	std::vector<GLBuffer> iBuffers_;
	std::vector<MeshInfo> meshInfo_;

	ModelData model_;
	Settings settings_;

	glm::vec2 modelOffset_;

	const std::vector<uint32_t> palette_;
	std::vector<std::future<void>> pngSaveResult_;
	std::vector<uint8_t> raster_;
	std::unique_ptr<IGlContext> glContext_;
};
