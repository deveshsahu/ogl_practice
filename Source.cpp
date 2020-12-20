#include "gl_core_4_5.h"
#include "glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/quaternion.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <vector>

struct ViewMatrix
{
	glm::mat4 view, projection, viewprojection;
};

struct LightInfo
{
	glm::vec4 lightDir;
	glm::vec4 La, Ld, Ls;
};

enum Settings
{
	ALL_OFF = 0,
	LIGHT_ON = 1 << 0,
	BUMP_ON = 1 << 1
};

class OglRenderer
{
public:
	OglRenderer(OglRenderer const&) = delete;
	void operator=(OglRenderer const&) = delete;

	static OglRenderer& getInstance()
	{
		static OglRenderer instance;
		return instance;
	}

	void init();
	void run();
	void cleanup();

	static void resizeCallback(GLFWwindow* window, int width, int height);
	static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

	void handleMouseMove(double xpos, double ypos);
	void handleMouseButton(int button, int action, int mods);

private:
	OglRenderer();

	void mGlInit();
	void mGlDraw();

	void mSetupGLSLProgram();
	void mSetupBuffers();
	void mLoadTextures();
	void mSetupRenderTarget();

private:
	glm::ivec2 mViewportSize;
	bool mViewportDirty = true;

	GLuint mPrg0ID = ~0; // Plain vertex shader, mono color frag

	GLuint mPrg1ID = ~0; // Plain vertex shader, texture frag shader

	GLuint mViewMatrixUniformIdx = ~0, mLightUniformIdx = ~0;

	GLuint  mvao = ~0;
	GLuint mVtxBuffer = ~0, mTexCoordBuffer = ~0, mNormBuffer = ~0, mTangBuffer = ~0;
	GLuint mDiffuseTexID = ~0, mNormalMapTexID = ~0;
	int mNumElements = 0;

	ViewMatrix mViewMat;
	LightInfo mLightInfo;

	int mSettings = ALL_OFF;

	GLFWwindow* window = nullptr;
	glm::dvec2 mPrevMouseLocation;
	bool mLeftMouseButtonPressed = false;

	glm::mat4 mWorldXform;

	GLuint mFBO = ~0;
};



int main()
{
	OglRenderer& renderer = OglRenderer::getInstance();

	renderer.init();
	renderer.run();
	renderer.cleanup();
}

OglRenderer::OglRenderer()
{
	mViewportSize.x = 800;
	mViewportSize.y = 600;
}

void OglRenderer::resizeCallback(GLFWwindow* window, int width, int height)
{
	auto& renderer = getInstance();
	renderer.mViewportSize.x = width;
	renderer.mViewportSize.y = height;
	renderer.mViewportDirty = true;
}

void OglRenderer::mouseMoveCallback(GLFWwindow* window, double xpos, double ypos)
{
	OglRenderer::getInstance().handleMouseMove(xpos, ypos);
}

void OglRenderer::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	OglRenderer::getInstance().handleMouseButton(button, action, mods);
}

void OglRenderer::handleMouseMove(double xpos, double ypos)
{
	if (mLeftMouseButtonPressed)
	{
		glm::rotate(glm::mat4(1.f), float(xpos-mPrevMouseLocation.x)*0.1f, glm::vec3(0, 0, 1));
	}
	mPrevMouseLocation.x = xpos;
	mPrevMouseLocation.y = ypos;
}

void OglRenderer::handleMouseButton(int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			mLeftMouseButtonPressed = true;
		}
		else if (action == GLFW_RELEASE)
		{
			mLeftMouseButtonPressed = false;
		}
	}
}

void OglRenderer::init()
{
	glfwInit();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

	window = glfwCreateWindow(mViewportSize.x, mViewportSize.y, "Practice", nullptr, nullptr);
	glfwSetFramebufferSizeCallback(window, resizeCallback);
	glfwMakeContextCurrent(window);
	mGlInit();
}

void OglRenderer::run()
{
	while (!glfwWindowShouldClose(window))
	{
		glfwMakeContextCurrent(window);


		mGlDraw();

		glfwSwapBuffers(window);
		glfwWaitEvents();
	}
}

void OglRenderer::cleanup()
{
	glfwDestroyWindow(window);
	glfwTerminate();
}

void mDebugCallback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam)
{
	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || severity == GL_DEBUG_SEVERITY_LOW)
		return;
	fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
		(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
		type, severity, message);
}

void OglRenderer::mGlInit()
{
	ogl_LoadFunctions();

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(mDebugCallback, NULL);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);

	mSetupGLSLProgram();

	mSetupBuffers();

	mViewMat.view = glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	mViewMat.projection = glm::perspective(glm::radians(30.f), (float)mViewportSize.x / mViewportSize.y, 0.001f, 1000.f);
	mViewMat.viewprojection = mViewMat.projection * mViewMat.view;
	glGenBuffers(1, &mViewMatrixUniformIdx);
	glBindBuffer(GL_UNIFORM_BUFFER, mViewMatrixUniformIdx);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(mViewMat), &mViewMat, GL_STATIC_DRAW);

	mSetupRenderTarget();

	mViewportDirty = false;

	mLightInfo.La = glm::vec4(1.f);
	mLightInfo.Ld = glm::vec4(1.f);
	mLightInfo.Ls = glm::vec4(1.f);
	mLightInfo.lightDir = glm::vec4(0, 0.0, 5, 1); // Point light source
	glGenBuffers(1, &mLightUniformIdx);
	glBindBuffer(GL_UNIFORM_BUFFER, mLightUniformIdx);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(mLightInfo), &mLightInfo, GL_STATIC_DRAW);

	mLoadTextures();
}

void OglRenderer::mGlDraw()
{
	if (mViewportDirty == true)
	{
		mViewMat.projection = glm::perspective(glm::radians(30.f), (float)mViewportSize.x / mViewportSize.y, 0.001f, 1000.f);
		mViewMat.viewprojection = mViewMat.projection * mViewMat.view;
		glBindBuffer(GL_UNIFORM_BUFFER, mViewMatrixUniformIdx);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(mViewMat), &mViewMat, GL_STATIC_DRAW);

		glDeleteFramebuffers(1, &mFBO);
		mSetupRenderTarget();
		mViewportDirty = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

	glViewport(0, 0, mViewportSize.x, mViewportSize.y);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glClearColor(0, 0, 0, 1);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_MULTISAMPLE);

	glUseProgram(mPrg0ID);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, mViewMatrixUniformIdx);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, mLightUniformIdx);

	mSettings |= LIGHT_ON;

	glm::vec3 Ka(1), Kd(1), Ks(1);
	float shininess = 120;

	// Front wall
	Ka = glm::vec3(0.8f);
	Kd = glm::vec3(0.8f);
	Ks = glm::vec3(1.f);
	glm::mat4 xform = glm::translate(glm::mat4(1.f), glm::vec3(0, 0.5, 0.5));
	glUniformMatrix4fv(0, 1, GL_FALSE, &xform[0][0]);
	glm::vec4 color(0.5, 0.5, 0.1, 1);
	glUniform4fv(1, 1, &color[0]);
	glUniform1i(2, mSettings);
	glUniform3fv(3, 1, &Ka[0]);
	glUniform3fv(4, 1, &Kd[0]);
	glUniform3fv(5, 1, &Ks[0]);
	glUniform1f(6, shininess);
	glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(mViewMat.view * xform)));
	glUniformMatrix3fv(7, 1, GL_FALSE, &normalMatrix[0][0]);
	glBindVertexArray(mvao);
	glDrawElements(GL_TRIANGLES, mNumElements, GL_UNSIGNED_INT, 0);

	// right wall
	Ka = glm::vec3(1);
	Kd = glm::vec3(1);
	Ks = glm::vec3(1);
	xform = glm::rotate(glm::radians(90.f), glm::vec3(0, 1, 0));
	xform = glm::translate(glm::mat4(1.f), glm::vec3(0.5, 0.5, 0.0)) * xform;
	glUniformMatrix4fv(0, 1, GL_FALSE, &xform[0][0]);
	color = glm::vec4(0.0, 0.0, 1, 1);
	glUniform4fv(1, 1, &color[0]);
	glUniform1i(2, mSettings);
	glUniform3fv(3, 1, &Ka[0]);
	glUniform3fv(4, 1, &Kd[0]);
	glUniform3fv(5, 1, &Ks[0]);
	glUniform1f(6, shininess);
	normalMatrix = glm::transpose(glm::inverse(glm::mat3(mViewMat.view * xform)));
	glUniformMatrix3fv(7, 1, GL_FALSE, &normalMatrix[0][0]);
	glDrawElements(GL_TRIANGLES, mNumElements, GL_UNSIGNED_INT, 0);

	// left wall
	xform = glm::rotate(glm::radians(-90.f), glm::vec3(0, 1, 0.0));
	xform = glm::translate(glm::mat4(1.f), glm::vec3(-0.5, 0.5, 0.0)) * xform;
	glUniformMatrix4fv(0, 1, GL_FALSE, &xform[0][0]);
	color = glm::vec4(1.0, 0.0, 0.0, 1);
	glUniform4fv(1, 1, &color[0]);
	glUniform1i(2, mSettings);
	glUniform3fv(3, 1, &Ka[0]);
	glUniform3fv(4, 1, &Kd[0]);
	glUniform3fv(5, 1, &Ks[0]);
	glUniform1f(6, shininess);
	normalMatrix = glm::transpose(glm::inverse(glm::mat3(mViewMat.view * xform)));
	glUniformMatrix3fv(7, 1, GL_FALSE, &normalMatrix[0][0]);
	glDrawElements(GL_TRIANGLES, mNumElements, GL_UNSIGNED_INT, 0);

	// back wall
	xform = glm::rotate(glm::radians(180.f), glm::vec3(0, 1, 0));
	xform = glm::translate(glm::mat4(1.f), glm::vec3(0.0, 0.5, -0.5)) * xform;
	glUniformMatrix4fv(0, 1, GL_FALSE, &xform[0][0]);
	color = glm::vec4(0.0, 1.0, 0, 1);
	glUniform4fv(1, 1, &color[0]);
	glUniform1i(2, mSettings);
	glUniform3fv(3, 1, &Ka[0]);
	glUniform3fv(4, 1, &Kd[0]);
	glUniform3fv(5, 1, &Ks[0]);
	glUniform1f(6, shininess);
	normalMatrix = glm::transpose(glm::inverse(glm::mat3(mViewMat.view * xform)));
	glUniformMatrix3fv(7, 1, GL_FALSE, &normalMatrix[0][0]);
	glDrawElements(GL_TRIANGLES, mNumElements, GL_UNSIGNED_INT, 0);

	glUseProgram(mPrg1ID);

	// Floor
	Ka = glm::vec3(0.4);
	Kd = glm::vec3(0);
	Ks = glm::vec3(1);
	xform = glm::rotate(glm::radians(-90.f), glm::vec3(1, 0, 0));
	xform *= glm::scale(glm::mat4(1.f), glm::vec3(3, 3, 1));
	glUniformMatrix4fv(0, 1, GL_FALSE, &xform[0][0]);
	glUniform1i(2, mSettings | BUMP_ON);
	glUniform3fv(3, 1, &Ka[0]);
	glUniform3fv(4, 1, &Kd[0]);
	glUniform3fv(5, 1, &Ks[0]);
	glUniform1f(6, shininess);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mDiffuseTexID);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mNormalMapTexID);
	normalMatrix = glm::transpose(glm::inverse(glm::mat3(mViewMat.view * xform)));
	glUniformMatrix3fv(7, 1, GL_FALSE, &normalMatrix[0][0]);
	glDrawElements(GL_TRIANGLES, mNumElements, GL_UNSIGNED_INT, 0);

	glBindVertexArray(0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glBlitNamedFramebuffer(mFBO, 0, 0, 0, mViewportSize.x, mViewportSize.y, 0, 0, mViewportSize.x, mViewportSize.y, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
}

void OglRenderer::mSetupGLSLProgram()
{
	const char* vtx_plain =
		"#version 450   \n\
layout (location = 0) in vec3 inVert; \n\
layout (location = 1) in vec2 inTexCoord; \n\
layout (location = 2) in vec3 inNorm;\n\
layout (location = 3) in vec3 inTang;\n\
layout (std140, binding = 0) uniform ViewMatrix \n\
{\n\
	mat4 view, projection, viewprojection; \n\
}viewmatrix; \n\
layout (std140, binding = 1) uniform Light \n\
{\n\
	vec4 lightDir;\n\
	vec4 La, Ld, Ls;\n\
}lightInfo; \n\
layout (location = 0) uniform mat4 modelMatrix; \n\
layout (location = 1) uniform vec4 color = vec4(0.8, 0.8, 0, 1); \n\
layout (location = 7) uniform mat3 normalMatrix; \n\
out VS_OUT \n\
{\n\
	vec3 pos;\n\
	vec4 color;\n\
	vec2 texCoord;\n\
	vec3 normal;\n\
	vec3 tangent;\n\
	vec3 lightpos;\n\
	vec3 viewDir;\n\
}vs_out;\n\
void main() \n\
{\n\
	vs_out.color = color;\n\
	vs_out.texCoord = inTexCoord; \n\
	vs_out.pos = vec3( viewmatrix.view * modelMatrix * vec4(inVert, 1.0)); \n\
	vs_out.normal = normalize(normalMatrix * inNorm);\n\
	vs_out.tangent = normalize(normalMatrix * inTang);\n\
	vec3 binormal = normalize(cross(vs_out.tangent, vs_out.normal));\n\
	mat3 tangentSpaceMat = mat3(\n\
		vs_out.tangent.x, vs_out.normal.x, binormal.x,\n\
		vs_out.tangent.y, vs_out.normal.y, binormal.y,\n\
		vs_out.tangent.z, vs_out.normal.z, binormal.z\n\
		);\n\
	vs_out.lightpos = tangentSpaceMat * (vec3(viewmatrix.view * vec4(lightInfo.lightDir.xyz , 1)) - vs_out.pos);\n\
	vs_out.viewDir = tangentSpaceMat * vec3(-vs_out.pos);\n\
	gl_Position = viewmatrix.viewprojection * modelMatrix * vec4(inVert, 1.0); \n\
}\0";
	const GLchar* vtx_plain_array[] = { vtx_plain };

	const char* frag_mono_color =
		"#version 450 \n\
#define LIGHT_ON 1<<0 \n\
in VS_OUT \n\
{\n \
	vec3 pos;\n\
	vec4 color;\n\
	vec2 texCoord;\n\
	vec3 normal;\n\
	vec3 tangent;\n\
	vec3 lightpos;\n\
	vec3 viewDir;\n\
}fs_in;\n\
layout (std140, binding = 0) uniform ViewMatrix \n\
{\n\
	mat4 view, projection, viewprojection; \n\
}viewmatrix; \n\
layout (std140, binding = 1) uniform Light \n\
{\n\
	vec4 lightDir;\n\
	vec4 La, Ld, Ls;\n\
}lightInfo; \n\
layout (location = 0) uniform mat4 modelMatrix = mat4(1.f); \n\
layout (location = 2) uniform int settings = 0; \n\
layout (location = 3) uniform vec3 Ka;\n\
layout (location = 4) uniform vec3 Kd;\n\
layout (location = 5) uniform vec3 Ks;\n\
layout (location = 6) uniform float shininess;\n\
layout (location = 7) uniform mat3 normalMatrix = mat3(1.f); \n\
out vec4 outColor; \n\
vec3 eval_lights() \n\
{\n\
	vec3 n = normalize(fs_in.normal);\n\
	vec3 s; \n\
	if (lightInfo.lightDir.w == 1) \n\
	{\n\
		s = normalize(vec3(viewmatrix.view * lightInfo.lightDir) - fs_in.pos); \n\
	} \n\
	else \n\
	{\n\
		s = normalize(normalMatrix * lightInfo.lightDir.xyz); \n\
	}\n\
	vec3 v = normalize(-fs_in.pos);\n\
	vec3 h = normalize(v+s);\n\
	return lightInfo.La.xyz * Ka + lightInfo.Ld.xyz * Kd * max(dot(s, fs_in.normal), 0.0) + lightInfo.Ls.xyz * Ks * pow(max(dot(h, n), 0.0), shininess); \n\
}\n\
void main() \n\
{ \n \
	if ((settings & LIGHT_ON) != 0)\n\
		outColor = vec4(eval_lights(), 1) * fs_in.color;\n\
	else \n\
		outColor = fs_in.color; \n\
}\0";
	const GLchar* frag_mono_color_array[] = { frag_mono_color };

	const char* frag_tex =
		"#version 450 \n \
#define LIGHT_ON 1<<0 \n\
#define BUMP_ON 1<<1 \n\
in VS_OUT \n \
{\n \
	vec3 pos;\n\
	vec4 color;\n\
	vec2 texCoord;\n\
	vec3 normal;\n\
	vec3 tangent;\n\
	vec3 lightpos;\n\
	vec3 viewDir;\n\
}fs_in;\n\
layout (std140, binding = 0) uniform ViewMatrix \n\
{\n\
	mat4 view, projection, viewprojection; \n\
}viewmatrix; \n\
layout (std140, binding = 1) uniform Light \n\
{\n\
	vec4 lightDir;\n\
	vec4 La, Ld, Ls;\n\
}lightInfo; \n\
layout (location = 0) uniform mat4 modelMatrix = mat4(1.f); \n\
layout (location = 2) uniform int settings = 0; \n\
layout (location = 3) uniform vec3 Ka;\n\
layout (location = 4) uniform vec3 Kd;\n\
layout (location = 5) uniform vec3 Ks;\n\
layout (location = 6) uniform float shininess;\n\
layout (binding = 0) uniform sampler2D diffuseTexture;\n\
layout (binding = 1) uniform sampler2D bumpTexture;\n\
layout (location = 7) uniform mat3 normalMatrix = mat3(1.f); \n\
out vec4 outColor; \n\
vec3 eval_lights_bump(in vec3 normal, in vec3 in_diffColor)\n\
{\n\
	vec3 n = normalize(normal);\n\
	vec3 h = normalize(fs_in.viewDir + fs_in.lightpos);\n\
	return lightInfo.La.xyz * Ka + lightInfo.Ld.xyz * Kd * max(dot(fs_in.lightpos, n), 0.0) * in_diffColor + lightInfo.Ls.xyz * Ks * pow(max(dot(h, n), 0.0), shininess); \n\
}\n\
vec3 eval_lights() \n\
{\n\
	vec3 n = normalize(fs_in.normal);\n\
	vec3 s; \n\
	if (lightInfo.lightDir.w == 1) \n\
	{\n\
		s = normalize(vec3(viewmatrix.view * lightInfo.lightDir) - fs_in.pos); \n\
	} \n\
	else \n\
	{\n\
		s = normalize(normalMatrix * lightInfo.lightDir.xyz); \n\
	}\n\
	vec3 v = normalize(-fs_in.pos);\n\
	vec3 h = normalize(v+s);\n\
	return lightInfo.La.xyz * Ka + lightInfo.Ld.xyz * Kd * max(dot(s, fs_in.normal), 0.0) + lightInfo.Ls.xyz * Ks * pow(max(dot(h, n), 0.0), shininess); \n\
}\n\
void main() \n\
{ \n \
	outColor = texture( diffuseTexture, fs_in.texCoord);\n\
	if ((settings & LIGHT_ON) != 0)\n\
	{\
		if ((settings & BUMP_ON) != 0) \n\
			outColor = vec4(eval_lights_bump(vec3(2 * texture(bumpTexture, fs_in.texCoord) - 1), outColor.rgb), 1);\n\
		else \n\
			outColor = vec4(eval_lights(), 1) * outColor;\n\
	}\
}\0";
	const GLchar* frag_tex_array[] = { frag_tex };

	auto vtx_plain_id = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vtx_plain_id, 1, vtx_plain_array, nullptr);
	glCompileShader(vtx_plain_id);
	GLint success;
	glGetShaderiv(vtx_plain_id, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char infolog[512];
		glGetShaderInfoLog(vtx_plain_id, 512, NULL, infolog);
		std::cout << infolog << std::endl;
	}

	auto frag_mono_color_id = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(frag_mono_color_id, 1, frag_mono_color_array, nullptr);
	glCompileShader(frag_mono_color_id);
	glGetShaderiv(frag_mono_color_id, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char infolog[512];
		glGetShaderInfoLog(frag_mono_color_id, 512, NULL, infolog);
		std::cout << infolog << std::endl;
	}

	auto frag_tex_id = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(frag_tex_id, 1, frag_tex_array, nullptr);
	glCompileShader(frag_tex_id);
	glGetShaderiv(frag_tex_id, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char infolog[512];
		glGetShaderInfoLog(frag_tex_id, 512, NULL, infolog);
		std::cout << infolog << std::endl;
	}

	mPrg0ID = glCreateProgram();
	glAttachShader(mPrg0ID, vtx_plain_id);
	glAttachShader(mPrg0ID, frag_mono_color_id);
	glLinkProgram(mPrg0ID);

	mPrg1ID = glCreateProgram();
	glAttachShader(mPrg1ID, vtx_plain_id);
	glAttachShader(mPrg1ID, frag_tex_id);
	glLinkProgram(mPrg1ID);

	glDeleteShader(vtx_plain_id);
	glDeleteShader(frag_mono_color_id);
	glDeleteShader(frag_tex_id);
}

void OglRenderer::mSetupBuffers()
{
	std::vector<glm::vec3> vtx = {
		glm::vec3(-0.5, 0.5, 0.0),
		glm::vec3(0.5, 0.5, 0.0),
		glm::vec3(0.5, -0.5, 0.0),
		glm::vec3(-0.5, -0.5, 0.0)
	};

	std::vector<unsigned int> idx = {
		0, 3, 2, 0, 2, 1
	};

	std::vector<glm::vec2> texCoord = {
		glm::vec2(0, 1),
		glm::vec2(1, 1),
		glm::vec2(1, 0),
		glm::vec2(0, 0)
	};

	std::vector<glm::vec3> norm = {
		glm::vec3(0, 0, 1),
		glm::vec3(0, 0, 1),
		glm::vec3(0, 0, 1),
		glm::vec3(0, 0, 1)
	};

	std::vector<glm::vec3> tang = {
		glm::vec3(1, 0, 0),
		glm::vec3(1, 0, 0),
		glm::vec3(1, 0, 0),
		glm::vec3(1, 0, 0)
	};
	mNumElements = (int)idx.size();

	glGenVertexArrays(1, &mvao);
	glBindVertexArray(mvao);

	glGenBuffers(1, &mVtxBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, mVtxBuffer);
	glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(glm::vec3), vtx.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glGenBuffers(1, &mTexCoordBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, mTexCoordBuffer);
	glBufferData(GL_ARRAY_BUFFER, texCoord.size() * sizeof(glm::vec2), texCoord.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);

	glGenBuffers(1, &mNormBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, mNormBuffer);
	glBufferData(GL_ARRAY_BUFFER, norm.size() * sizeof(glm::vec3), norm.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(2);

	glGenBuffers(1, &mTangBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, mTangBuffer);
	glBufferData(GL_ARRAY_BUFFER, tang.size() * sizeof(glm::vec3), tang.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(3);

	GLuint ebo;
	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

}

void OglRenderer::mLoadTextures()
{
	int width, height, nchannels;
	unsigned char* data = stbi_load("textures/green_grass.jpg", &width, &height, &nchannels, 0);
	glGenTextures(1, &mDiffuseTexID);
	glBindTexture(GL_TEXTURE_2D, mDiffuseTexID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	data = nullptr; width = 0, height = 0, nchannels = 0;
	data = stbi_load("textures/green_grass_normalmap.png", &width, &height, &nchannels, 0);
	glGenTextures(1, &mNormalMapTexID);
	glBindTexture(GL_TEXTURE_2D, mNormalMapTexID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void OglRenderer::mSetupRenderTarget()
{
	glGenFramebuffers(1, &mFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

	GLuint colorTexID, depthTexID;
	glGenTextures(1, &colorTexID);
	glBindTexture(GL_TEXTURE_2D, colorTexID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mViewportSize.x, mViewportSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexID, 0);

	glGenTextures(1, &depthTexID);
	glBindTexture(GL_TEXTURE_2D, depthTexID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, mViewportSize.x, mViewportSize.y, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexID, 0);
}
