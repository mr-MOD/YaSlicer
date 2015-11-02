#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include "GlContext.h"

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
		queue(std::max(1u, std::thread::hardware_concurrency())),
		plateWidth(96.0f), plateHeight(54.0f),
		doAxialDilate(true), doOmniDirectionalDilate(false), omniDilateSliceFactor(1), omniDilateScale(1.0f),
		modelOffset(0), optimizeMesh(true), doBinarize(false), binarizeThreshold(0) {}

	bool offscreen;
	std::string modelFile;
	std::array<std::string, 2> machineMaskFile;

	std::string outputDir;

	float step;

	uint32_t renderWidth;
	uint32_t renderHeight;

	uint32_t samples;
	uint32_t queue;

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
};

class Renderer
{
	struct GLData
	{
		GLData();
		~GLData();

		GLuint mainProgram;
		GLuint mainVertexPosAttrib;
		GLuint mainTransformUniform;
		GLuint mainMirrorUniform;

		GLuint maskProgram;
		GLuint maskVertexPosAttrib;
		GLuint maskWVPTransformUniform;
		GLuint maskWVTransformUniform;
		GLuint maskTextureUniform;
		GLuint maskPlateSizeUniform;
		
		std::array<GLuint, 2> machineMaskTexture;

		std::vector<GLuint> vBuffers;
		std::vector<GLuint> iBuffers;

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
		std::vector<TriangleData> iCount;
	};

	struct ModelData
	{
		ModelData() : pos()
		{
		}

		float pos;
		glm::vec3 min;
		glm::vec3 max;
	};
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

private:
	void CreateGeometryBuffers();
	void LoadMasks();

	void Render();
	void RenderCommon();
	void RenderOffscreen();
	void RenderFullscreen();

	void Model(const glm::mat4x4& wvpMatrix);
	void Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix);

	GLData glData_;
	ModelData model_;
	Settings settings_;
	uint32_t curMask_;

	GLenum cullFront_;
	GLenum cullBack_;
	glm::vec2 mirror_;

	std::vector<std::future<void>> pngSaveResult_;
	std::vector<uint8_t> raster_;
	std::unique_ptr<IGlContext> glContext_;
};
