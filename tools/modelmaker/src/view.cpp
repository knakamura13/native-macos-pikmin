#include "view.h"

// La ecuación de luz de GX del juego (canal 0), la misma que usa pc_gfx.cpp.
// Por ruta: la carpeta pc_port tiene cabeceras (wingdi.h) que no deben
// tapar las del sistema.
#include "../../../games/pikmin1/pc_port/gl/pc_gx_lighting_glsl.h"

#include <SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

namespace mm {

namespace {

const char* kVertexHead = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aCol;
uniform mat4 uMvp;
uniform int uGame;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUv;
out vec4 vCol;
out vec3 vLit;
)";
const char* kVertexBody = R"(
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
	vNormal = aNrm;
	vWorldPos = aPos;
	vUv = aUv;
	vCol = aCol;
	// Por vértice, como el juego con la opción "Original".
	vLit = vec3(1.0);
	if (uGame != 0) vLit = gxLit0(length(aNrm) > 1e-5 ? normalize(aNrm) : vec3(0.0), vec4(aPos, 1.0));
})";

// Sin uGame, luz parecida a la del juego: una principal desde arriba y
// delante, otra suave desde atrás, y ambiente. Con uGame, la del juego al
// dibujar un pack HD (pc_hd_models.cpp, drawModel): textura × color del
// material × luz, sin descartar por alfa.
const char* kFragmentHead = R"(#version 330 core
in vec2 vUv;
in vec4 vCol;
in vec3 vLit;
uniform sampler2D uTex;
uniform int uUseTex;
uniform float uLit;
uniform float uAlpha;
uniform int uGame;
uniform vec4 uMatColor;
out vec4 frag;
)";
const char* kFragmentBody = R"(
void main() {
	vec4 c = vCol;
	if (uUseTex != 0) c *= texture(uTex, vUv);
	if (uGame != 0) {
		if (uLit > 0.5) c.rgb *= uMatColor.rgb * gxPixelLit0(vLit);
		frag = vec4(c.rgb, c.a * uAlpha);
		return;
	}
	if (c.a < 0.04) discard;
	if (uLit > 0.5) {
		vec3 n = normalize(vNormal);
		float l = 0.38 + 0.55 * max(dot(n, normalize(vec3(0.35, 0.8, 0.6))), 0.0) + 0.22 * max(dot(n, normalize(vec3(-0.5, 0.3, -0.7))), 0.0);
		c.rgb *= min(l, 1.15);
	}
	frag = vec4(c.rgb, c.a * uAlpha);
})";

GLuint compile(GLenum type, const char* src, std::string& error)
{
	const GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		error = log;
		glDeleteShader(s);
		return 0;
	}
	return s;
}

} // namespace

Vec3 OrbitCamera::eye() const
{
	const float cp = std::cos(pitch);
	return target + Vec3 { std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp } * distance;
}

nm::M4 OrbitCamera::view() const
{
	const Vec3 e = eye();
	return nm::M4::lookAt({ e.x, e.y, e.z }, { target.x, target.y, target.z }, { 0, 1, 0 });
}

nm::M4 OrbitCamera::projection(float aspect) const { return nm::M4::perspective(fovY, aspect, distance * 0.02f, distance * 20.0f); }

void DynamicMesh::upload()
{
	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, p)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, n)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, uv)));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, c)));
	}
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * sizeof(GpuVertex)), data.data(), GL_STREAM_DRAW);
	glBindVertexArray(0);
}

DynamicMesh::~DynamicMesh()
{
	if (vbo) glDeleteBuffers(1, &vbo);
	if (vao) glDeleteVertexArrays(1, &vao);
}

bool View::init(std::string& error)
{
	const std::string vertex   = std::string(kVertexHead) + kGxLightingGlsl + kVertexBody;
	const std::string fragment = std::string(kFragmentHead) + kGxLightingGlsl + kGxLightingFragGlsl + kFragmentBody;
	const GLuint vs = compile(GL_VERTEX_SHADER, vertex.c_str(), error);
	if (!vs) return false;
	const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment.c_str(), error);
	if (!fs) return false;
	program_ = glCreateProgram();
	glAttachShader(program_, vs);
	glAttachShader(program_, fs);
	glLinkProgram(program_);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program_, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
		error = log;
		return false;
	}
	uMvp_    = glGetUniformLocation(program_, "uMvp");
	uUseTex_ = glGetUniformLocation(program_, "uUseTex");
	uLit_    = glGetUniformLocation(program_, "uLit");
	uAlpha_  = glGetUniformLocation(program_, "uAlpha");
	uTex_    = glGetUniformLocation(program_, "uTex");
	uGame_       = glGetUniformLocation(program_, "uGame");
	uMatColor_   = glGetUniformLocation(program_, "uMatColor");
	uNumLights_  = glGetUniformLocation(program_, "uNumLights");
	uLightPos_   = glGetUniformLocation(program_, "uLightPos");
	uLightColor_ = glGetUniformLocation(program_, "uLightColor");
	uLightK_     = glGetUniformLocation(program_, "uLightK");
	uAmbColor_   = glGetUniformLocation(program_, "uAmbColor");
	uChan0En_    = glGetUniformLocation(program_, "uChan0En");
	uPerPixel_   = glGetUniformLocation(program_, "uPerPixel");
	nectar::Image w;
	w.resize(2, 2);
	std::fill(w.rgba.begin(), w.rgba.end(), std::uint8_t(255));
	white_ = uploadTexture(w, true);
	return true;
}

void View::resize(int w, int h)
{
	w = std::max(w, 1), h = std::max(h, 1);
	if (w == width_ && h == height_ && fbo_) return;
	width_ = w, height_ = h;
	if (!fbo_) {
		glGenFramebuffers(1, &fbo_);
		glGenTextures(1, &color_);
		glGenRenderbuffers(1, &depth_);
	}
	glBindTexture(GL_TEXTURE_2D, color_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindRenderbuffer(GL_RENDERBUFFER, depth_);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void View::begin(const nm::M4& viewProj, const GameLook* look)
{
	viewProj_ = viewProj;
	look_     = look;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glViewport(0, 0, width_, height_);
	// Fondo neutro como el de los editores 3D: permite juzgar colores y
	// transparencias sin que el verde de la interfaz contamine el modelo.
	glClearColor(0.105f, 0.12f, 0.125f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(program_);
	glUniformMatrix4fv(uMvp_, 1, GL_FALSE, viewProj_.m);
	glUniform1i(uTex_, 0);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(uGame_, look_ ? 1 : 0);
	if (look_) {
		// Canal 0 encendido con GX_AF_SPOT y atenuación 1, 0, 0 (Light::update
		// de las paralelas); el alfa sin luz, como en drawModel.
		float pos[16] = {}, colour[16] = {}, k[16] = {};
		const int n = int(std::min<std::size_t>(look_->lights.size(), 4));
		for (int i = 0; i < n; ++i) {
			const GameLook::Light& l = look_->lights[std::size_t(i)];
			pos[i * 4] = l.pos.x, pos[i * 4 + 1] = l.pos.y, pos[i * 4 + 2] = l.pos.z;
			colour[i * 4] = l.colour[0], colour[i * 4 + 1] = l.colour[1], colour[i * 4 + 2] = l.colour[2], colour[i * 4 + 3] = 1;
			k[i * 4] = 1, k[i * 4 + 3] = 1;
		}
		glUniform1i(uChan0En_, 1);
		glUniform1i(uNumLights_, n);
		glUniform4fv(uLightPos_, 4, pos);
		glUniform4fv(uLightColor_, 4, colour);
		glUniform4fv(uLightK_, 4, k);
		glUniform4f(uAmbColor_, look_->ambient[0], look_->ambient[1], look_->ambient[2], 1);
		glUniform1i(uPerPixel_, look_->perPixel ? 1 : 0);
	}
}

void View::drawMesh(const DynamicMesh& m, bool lit, float alpha)
{
	if (!m.vao) return;
	glUniform1f(uLit_, lit ? 1.0f : 0.0f);
	glUniform1f(uAlpha_, alpha);
	glBindVertexArray(m.vao);
	// Lo opaco primero; lo translúcido después sin escribir profundidad.
	for (int pass = 0; pass < 2; ++pass) {
		glDepthMask(pass == 0 && alpha >= 1.0f ? GL_TRUE : GL_FALSE);
		for (const DynamicMesh::Range& r : m.ranges) {
			if (r.blend != (pass == 1) || !r.count) continue;
			glUniform1i(uUseTex_, r.texture ? 1 : 0);
			glBindTexture(GL_TEXTURE_2D, r.texture ? r.texture : white_);
			if (look_) {
				// Los triángulos del pack van en sentido antihorario: GX_CULL_FRONT
				// del juego (con glFrontFace(GL_CW)) es quitar GL_BACK aquí.
				if (r.cull) glEnable(GL_CULL_FACE);
				else glDisable(GL_CULL_FACE);
				glCullFace(GL_BACK);
				const float* t = look_->tint;
				if (r.tint) glUniform4f(uMatColor_, t[0], t[1], t[2], 1);
				else glUniform4f(uMatColor_, 1, 1, 1, 1);
			}
			glDrawArrays(GL_TRIANGLES, r.first, r.count);
		}
	}
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glBindVertexArray(0);
}

void View::drawLines(const std::vector<Vec3>& pairs, const float colour[4])
{
	lines_.data.clear();
	for (const Vec3& p : pairs) {
		GpuVertex v {};
		v.p[0] = p.x, v.p[1] = p.y, v.p[2] = p.z;
		v.n[1] = 1;
		for (int i = 0; i < 4; ++i) v.c[i] = colour[i];
		lines_.data.push_back(v);
	}
	lines_.upload();
	glUniform1f(uLit_, 0.0f);
	glUniform1f(uAlpha_, 1.0f);
	glUniform1i(uUseTex_, 0);
	glBindVertexArray(lines_.vao);
	glDrawArrays(GL_LINES, 0, GLsizei(lines_.data.size()));
	glBindVertexArray(0);
}

void View::end()
{
	look_ = nullptr;
	glUniform1i(uGame_, 0);
	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool View::renderImage(int w, int h, const nm::M4& viewProj, const DynamicMesh& mesh, nectar::Image& out)
{
	GLuint fbo = 0, tex = 0, depth = 0;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &tex);
	glGenRenderbuffers(1, &depth);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	if (ok) {
		glViewport(0, 0, w, h);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDisable(GL_CULL_FACE);
		// El alfa del fondo no se mezcla con el del modelo.
		static const auto blendSeparate = reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(SDL_GL_GetProcAddress("glBlendFuncSeparate"));
		if (blendSeparate) blendSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		glUseProgram(program_);
		glUniformMatrix4fv(uMvp_, 1, GL_FALSE, viewProj.m);
		glUniform1i(uTex_, 0);
		glUniform1i(uGame_, 0);
		glActiveTexture(GL_TEXTURE0);
		drawMesh(mesh, true);
		nectar::Image img;
		img.resize(w, h);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
		out.resize(w, h);
		for (int y = 0; y < h; ++y) std::copy(img.at(0, h - 1 - y), img.at(0, h - 1 - y) + w * 4, out.at(0, y));
		glUseProgram(0);
		glDisable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteRenderbuffers(1, &depth);
	glDeleteTextures(1, &tex);
	return ok;
}

View::~View()
{
	if (fbo_) glDeleteFramebuffers(1, &fbo_);
	if (depth_) glDeleteRenderbuffers(1, &depth_);
	if (color_) glDeleteTextures(1, &color_);
	if (white_) glDeleteTextures(1, &white_);
}

GLuint uploadTexture(const nectar::Image& img, bool repeat)
{
	if (img.empty()) return 0;
	GLuint t = 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	return t;
}

} // namespace mm
