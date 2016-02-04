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
	Settings() : offscreen(true), step(0.025f), renderWidth(1920), renderHeight(1080), samples(0),
		queue(std::max(1u, std::thread::hardware_concurrency())), whiteLayers(1),
		plateWidth(96.0f), plateHeight(54.0f),
		doAxialDilate(true), doOmniDirectionalDilate(false), omniDilateSliceFactor(1), omniDilateScale(1.0f),
		modelOffset(0), optimizeMesh(true), doBinarize(false), binarizeThreshold(0),
		mirrorX(false), mirrorY(false),
		doOverhangAnalysis(false), maxSupportedDistance(0.1f), enableERM(false),
		envisiontechTemplatesPath("envisiontech") {}

	bool offscreen;
	std::string modelFile;
	std::array<std::string, 2> machineMaskFile;

	std::string outputDir;

	float step;

	uint32_t renderWidth;
	uint32_t renderHeight;

	uint32_t samples;
	uint32_t queue;
	uint32_t whiteLayers;

	float plateWidth;
	float plateHeight;

	bool doAxialDilate;
	bool doOmniDirectionalDilate;

	uint32_t omniDilateSliceFactor;
	float omniDilateScale;

	bool doBinarize;
	uint32_t binarizeThreshold;

	glm::vec2 modelOffset;

	bool optimizeMesh;

	bool doOverhangAnalysis;
	float maxSupportedDistance;

	bool enableERM;
	std::string envisiontechTemplatesPath;

	bool mirrorX;
	bool mirrorY;
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
	void AnalyzeOverhangs();

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

	struct TriangleData
	{
		TriangleData() :
			frontFacing(0), orthoFacing(0), backFacing(0)
		{
		}
		TriangleData(GLsizei frontFacing, GLsizei orthoFacing, GLsizei backFacing) :
			frontFacing(frontFacing), orthoFacing(orthoFacing), backFacing(backFacing)
		{
		}
		GLsizei frontFacing;
		GLsizei orthoFacing;
		GLsizei backFacing;
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
	uint32_t GetCurrentSliceERM() const;

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

	std::vector<TriangleData> iCount_;

	ModelData model_;
	Settings settings_;
	uint32_t curMask_;

	GLenum cullFront_;
	GLenum cullBack_;
	glm::vec2 mirror_;

	const std::vector<uint32_t> palette_;
	std::vector<std::future<void>> pngSaveResult_;
	std::vector<uint8_t> raster_;
	std::unique_ptr<IGlContext> glContext_;
};
