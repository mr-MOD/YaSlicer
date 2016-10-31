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
	std::array<std::string, 2> machineMaskFile;

	std::string outputDir;

	float step = 0.025f;

	uint32_t renderWidth = 1920;
	uint32_t renderHeight = 1080;

	uint32_t samples = 0;
	uint32_t queue = std::max(1u, std::thread::hardware_concurrency());
	uint32_t whiteLayers = 1;

	float plateWidth = 96.0f;
	float plateHeight = 54.0f;

	bool doAxialDilate = false;
	bool doOmniDirectionalDilate = false;

	bool doInflate = false;
	float inflateDistance = 0.1f;

	uint32_t omniDilateSliceFactor = 1;
	float omniDilateScale = 1.0f;

	bool doBinarize = false;
	uint32_t binarizeThreshold = 128;

	glm::vec2 modelOffset = glm::vec2(0, 0);

	bool doOverhangAnalysis = false;
	float maxSupportedDistance = 0.5f;

	bool enableERM = false;
	std::string envisiontechTemplatesPath = "envisiontech";
	
	bool doSmallSpotsProcessing = false;
	float smallSpotThreshold = 1.0f;
	float smallSpotColorScaleFactor = 1.0f;
	bool dilateOnlySmallSpots = false;

	bool mirrorX = false;
	bool mirrorY = false;
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
	void Black();
	void White();
	void SetMask(uint32_t n);
	void MirrorX();
	void MirrorY();
	void ERM();
	void AnalyzeOverhangs(uint32_t imageNumber);

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

	using UniformSetterType = std::function<void(const GLProgram&)>;
	using UniformSetters = std::vector<UniformSetterType>;

	void CreateGeometryBuffers();
	void LoadMasks();

	void Render();
	void RenderCommon();
	void RenderAxialDilate();
	void RenderOmniDilate(float scale, uint32_t kernelSize);
	void RenderBinarize(uint32_t threshold);
	void RenderDifference();
	void Render2DFilter(const GLProgram& program, const UniformSetters& additionalUniformSetters = UniformSetters());
	void RenderOffscreen();
	void RenderFullscreen();

	void Model(const glm::mat4x4& wvpMatrix);
	void Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix);

	uint32_t GetCurrentSlice() const;

	GLProgram mainProgram_;
	GLuint mainVertexPosAttrib_;
	GLuint mainTransformUniform_;
	GLuint mainMirrorUniform_;

	GLProgram maskProgram_;
	GLuint maskVertexPosAttrib_;
	GLuint maskWVPTransformUniform_;
	GLuint maskWVTransformUniform_;
	GLuint maskTextureUniform_;
	GLuint maskPlateSizeUniform_;

	GLProgram axialDilateProgram_;
	GLProgram omniDilateProgram_;
	GLProgram binarizeProgram_;
	GLProgram differenceProgram_;

	GLTexture machineMaskTexture_[2];

	GLTexture whiteTexture_;

	GLFramebuffer imageFBO_;
	GLTexture imageTexture_;

	GLFramebuffer previousLayerImageFBO_;
	GLTexture previousLayerImageTexture_;

	GLFramebuffer differenceFBO_;
	GLTexture differenceTexture_;

	std::vector<GLBuffer> vBuffers_;
	std::vector<GLBuffer> iBuffers_;
	std::vector<GLsizei> triCount_;

	ModelData model_;
	Settings settings_;
	uint32_t curMask_;

	glm::vec2 mirror_;

	const std::vector<uint32_t> palette_;
	std::vector<std::future<void>> pngSaveResult_;
	std::vector<uint8_t> raster_;
	std::unique_ptr<IGlContext> glContext_;
};
