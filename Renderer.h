#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#ifdef HAVE_LIBBCM_HOST
#include <bcm_host.h>
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <string>
#include <vector>
#include <array>
#include <future>

#include <stdint.h>

#undef min
#undef max

struct Settings
{
	Settings() : step(0.025f), renderWidth(1920), renderHeight(1080), samples(0),
		queue(std::max(1u, std::thread::hardware_concurrency())),
		plateWidth(96.0f), plateHeight(54.0f), downScaleCount(0),
		dilateCount(0), dilateSliceFactor(1), modelOffset(0.5f) {}

	std::string stlFile;
	std::array<std::string, 2> machineMaskFile;

	std::string outputDir;

	float step;

	uint32_t renderWidth;
	uint32_t renderHeight;

	uint32_t samples;
	uint32_t queue;

	float plateWidth;
	float plateHeight;

	uint32_t downScaleCount;
	uint32_t dilateCount;
	uint32_t dilateSliceFactor;
	float modelOffset;
};

class Renderer
{
	struct GLData
	{
		GLData();
		~GLData();

		EGLDisplay display;
		EGLContext context;
		EGLSurface surface;

		GLuint mainProgram;
		GLuint mainVertexPosAttrib;
		GLuint mainTransformUniform;

		GLuint maskProgram;
		GLuint maskVertexPosAttrib;
		GLuint maskWVPTransformUniform;
		GLuint maskWVTransformUniform;
		GLuint maskTextureUniform;
		GLuint maskPlateSizeUniform;

		GLuint dilateProgram;
		GLuint dilateVertexPosAttrib;
		GLuint dilateTexelSizeUniform;
		GLuint dilateTextureUniform;

		GLuint downScaleProgram;
		GLuint downScaleVertexPosAttrib;
		GLuint downScaleTexelSizeUniform;
		GLuint downScaleTextureUniform;
		GLuint downScaleLodUniform;
		
		std::array<GLuint, 2> machineMaskTexture;
		GLuint renderTexture;
		GLuint textureFBO;

		GLuint renderBuffer;
		GLuint renderBufferDepth;
		GLuint fbo;

		std::vector<GLuint> vBuffersFront;
		std::vector<GLuint> vBuffersBack;
		std::vector<GLuint> vBuffersVertical;

		std::vector<GLuint> iBuffersFront;
		std::vector<GLuint> iBuffersBack;
		std::vector<GLuint> iBuffersVertical;
	};

	struct ModelData
	{
		ModelData();

		float pos;
		uint32_t numFront;
		uint32_t numBack;
		uint32_t numVertical;

		glm::vec3 min;
		glm::vec3 max;

		std::vector<float> geometry;
	};
public:
	Renderer(const Settings& settings);
	~Renderer();
#ifdef WIN32
	void SavePng(const std::string& fileName);
#endif // WIN32

	void FirstSlice();
	bool NextSlice();
	void Black();
	void White();
	void SetMask(uint32_t n);

private:
	void LoadStl();
	void LoadObj(const std::string& file, std::vector<float>& vb, std::vector<int>& ib);
	void LoadMasks();
	
	uint32_t GetLayersCount() const;

	void Render();

	void Model(const glm::mat4x4& wvpMatrix);
	void Mask(const glm::mat4x4& wvpMatrix, const glm::mat4x4& wvMatrix);
	void Dilate();
	void Downscale();
	void Blit(GLuint fboFrom, GLuint fboTo);

	GLData glData_;
	ModelData model_;
	Settings settings_;
	uint32_t curMask_;

#ifdef HAVE_LIBBCM_HOST
	EGL_DISPMANX_WINDOW_T nativeWindow_;
#endif

#ifdef WIN32
	std::vector<std::future<void>> pngSaveResult_;
#endif // WIN32
	std::vector<uint8_t> tempPixelBuffer_;
};
