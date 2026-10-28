// Cena com iluminação direcional, projetor, fog e plano de corte.
// A esfera texturizada usa um mapa de normais.

#include <GLFW/glfw3.h>

#include "wgpucontext.h"
#include "arcball.h"
#include "scene.h"
#include "state.h"
#include "camera3d.h"
#include "phongmaterial.h"
#include "directionallight.h"
#include "texture.h"
#include "sampler.h"
#include "textureset.h"
#include "transform.h"
#include "node.h"
#include "cube.h"
#include "sphere.h"
#include "shader.h"
#include "pipeline.h"
#include "renderer.h"
#include "error.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

static float viewer_pos[3] = {2.5f, 4.5f, 3.0f};

static WgpuContextPtr ctx;
static RendererPtr renderer;
static ScenePtr scene;
static Camera3DPtr camera;
static ArcballPtr arcball;
static DirectionalLightPtr light;

namespace {
// Diretório local das texturas.
const char* kTexturesDir = R"(C:\Users\pribe\Documents\Estudos-Computacao-Grafica\textures\)";

constexpr float kTableSurfaceY = 1.0f;
constexpr float kTableThickness = 0.2f;
constexpr float kLegHeight = kTableSurfaceY - kTableThickness;
constexpr float kLegInsetX = 1.0f;
constexpr float kLegInsetZ = 0.7f;

float clipY = 1.7f;
constexpr float kClipStep = 0.05f;
constexpr float kClipMin = 0.0f;
constexpr float kClipMax = 3.0f;

constexpr double kBackground[4] = {0.70, 0.75, 0.80, 1.0};
} // namespace

static NodePtr MakeBox (CubePtr cube, std::initializer_list<AppearancePtr> apps,
                        float sx, float sy, float sz, float tx, float ty, float tz)
{
  TransformPtr trf = Transform::Make();
  trf->Translate(tx, ty, tz);
  trf->Scale(sx, sy, sz);
  return Node::Make(trf, apps, {cube});
}

static NodePtr MakeSphere (SpherePtr sphere, std::initializer_list<AppearancePtr> apps,
                           float radius, float tx, float ty, float tz)
{
  TransformPtr trf = Transform::Make();
  trf->Translate(tx, ty, tz);
  trf->Scale(radius, radius, radius);
  return Node::Make(trf, apps, {sphere});
}

static void initialize (void)
{
  camera = Camera3D::Make(viewer_pos[0], viewer_pos[1], viewer_pos[2]);
  arcball = camera->CreateArcball();

  // Luz direcional; o projetor é calculado no shader.
  light = DirectionalLight::Make(-0.5f, -1.0f, -0.4f, "world");
  light->SetAmbient(0.15f, 0.15f, 0.15f);
  light->SetDiffuse(0.55f, 0.58f, 0.65f);
  light->SetSpecular(0.6f, 0.6f, 0.6f);
  light->Set("clip_height", clipY);

  // As texturas decal fornecem a cor dos objetos.
  PhongMaterialPtr white = PhongMaterial::Make(1.0f, 1.0f, 1.0f);

  // Texturas decal e mapas de normais usam o mesmo sampler.
  std::string dir(kTexturesDir);
  SamplerPtr sampler = Sampler::Make(ctx->GetDevice(), "decal_sampler");
  TexturePtr woodTex  = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "wood3d.jpg");
  TexturePtr stoneTex = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "stone3d.png");
  TexturePtr earthTex = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "earth3d.jpg");
  TexturePtr brickTex = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "text_brick.jpg");

  TexturePtr roughNormalTex = Texture::Make(ctx->GetDevice(), "normal_texture", dir + "rugosidade_brick.jpg");
  // O shader ignora o mapa de normais 1x1.
  TexturePtr flatNormalTex  = Texture::Make(ctx->GetDevice(), "normal_texture", Vec3{1.0f, 1.0f, 1.0f});

  TexturePtr logoTex = Texture::Make(ctx->GetDevice(), "slide_texture", dir + "puc-rio_logo.png");

  TextureSetPtr woodSet  = TextureSet::Make({woodTex.get(),  flatNormalTex.get(),  logoTex.get(), sampler.get()});
  TextureSetPtr stoneSet = TextureSet::Make({stoneTex.get(), flatNormalTex.get(),  logoTex.get(), sampler.get()});
  TextureSetPtr earthSet = TextureSet::Make({earthTex.get(), flatNormalTex.get(),  logoTex.get(), sampler.get()});
  TextureSetPtr brickRoughSet = TextureSet::Make({brickTex.get(), roughNormalTex.get(), logoTex.get(), sampler.get()});

  CubePtr cube = Cube::Make(ctx->GetDevice());
  SpherePtr sphere = Sphere::Make(ctx->GetDevice());

  ShaderPtr shader = Shader::Make(ctx->GetDevice(), "../shaders/ilum_frag/mesa.wgsl", light, "world");

  // Layout de vértices compartilhado pelas malhas.
  VertexBufferSpec coordBuf, normalBuf, tangentBuf, texcoordBuf;
  coordBuf.arrayStride = 3 * 4;
  coordBuf.attributes.push_back({std::string("coord"), std::nullopt, WGPUVertexFormat_Float32x3, 0});
  normalBuf.arrayStride = 3 * 4;
  normalBuf.attributes.push_back({std::string("normal"), std::nullopt, WGPUVertexFormat_Float32x3, 0});
  tangentBuf.arrayStride = 3 * 4;
  tangentBuf.attributes.push_back({std::nullopt, (int) Shape::TANGENT, WGPUVertexFormat_Float32x3, 0});
  texcoordBuf.arrayStride = 2 * 4;
  texcoordBuf.attributes.push_back({std::string("texcoord"), std::nullopt, WGPUVertexFormat_Float32x2, 0});
  shader->SetVertexBuffers({coordBuf, normalBuf, tangentBuf, texcoordBuf});

  PipelineDesc pdesc;
  WGPUPrimitiveState primitive = {};
  primitive.cullMode = WGPUCullMode_Back;
  pdesc.primitive = primitive;
  PipelinePtr pipeline = Pipeline::Make(shader, ctx->GetSurfaceFormat(), pdesc);

  shader->AddMaterial(white.get());
  shader->AddTextureSet(woodSet.get());
  shader->AddTextureSet(stoneSet.get());
  shader->AddTextureSet(earthSet.get());
  shader->AddTextureSet(brickRoughSet.get());

  // Mesa
  NodePtr tableTop = MakeBox(cube, {white, woodSet}, 2.4f, kTableThickness, 1.8f,
                             0.0f, kTableSurfaceY - kTableThickness, 0.0f);
  NodePtr legFrontLeft  = MakeBox(cube, {white, woodSet}, 0.15f, kLegHeight, 0.15f,
                                  -kLegInsetX, 0.0f, kLegInsetZ);
  NodePtr legFrontRight = MakeBox(cube, {white, woodSet}, 0.15f, kLegHeight, 0.15f,
                                  kLegInsetX, 0.0f, kLegInsetZ);
  NodePtr legBackLeft   = MakeBox(cube, {white, woodSet}, 0.15f, kLegHeight, 0.15f,
                                  -kLegInsetX, 0.0f, -kLegInsetZ);
  NodePtr legBackRight  = MakeBox(cube, {white, woodSet}, 0.15f, kLegHeight, 0.15f,
                                  kLegInsetX, 0.0f, -kLegInsetZ);

  // Caixa
  float boxHeight = 0.5f;
  NodePtr box = MakeBox(cube, {white, stoneSet}, 0.8f, boxHeight, 0.8f,
                        -0.3f, kTableSurfaceY, 0.0f);
  float boxTopY = kTableSurfaceY + boxHeight;

  // Esfera texturizada sobre a caixa
  float earthRadius = 0.3f;
  NodePtr earthSphere = MakeSphere(sphere, {white, earthSet}, earthRadius,
                                   -0.3f, boxTopY + earthRadius, 0.0f);

  float ballRadius = 0.4f;
  NodePtr ballSphere = MakeSphere(sphere, {white, brickRoughSet}, ballRadius,
                                  0.5f, kTableSurfaceY + ballRadius, -0.1f);

  NodePtr root = Node::Make(pipeline, {
    tableTop, legFrontLeft, legFrontRight, legBackLeft, legBackRight,
    box, earthSphere, ballSphere,
  });
  scene = Scene::Make(root);
}

static void display ()
{
  WGPUSurfaceTexture surfaceTex = ctx->GetCurrentTexture();
  if (surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    return;
  renderer->Render(surfaceTex.texture, scene, camera);
  ctx->Present();
  wgpuTextureRelease(surfaceTex.texture);
}

static void error (int code, const char* msg)
{
  printf("GLFW error %d: %s\n", code, msg);
  glfwTerminate();
  exit(0);
}

static void keyboard (GLFWwindow* window, int key, int scancode, int action, int mods)
{
  (void) scancode; (void) mods;
  if (key == GLFW_KEY_Q && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GLFW_TRUE);

  if ((action == GLFW_PRESS || action == GLFW_REPEAT) &&
      (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)) {
    clipY += (key == GLFW_KEY_UP) ? kClipStep : -kClipStep;
    clipY = std::min(kClipMax, std::max(kClipMin, clipY));
    light->Set("clip_height", clipY);
    printf("plano de corte: y = %.2f\n", clipY);
  }
}

static void resize (GLFWwindow* win, int width, int height)
{
  (void) win;
  if (width > 0 && height > 0) ctx->Configure(width, height);
}

static void cursorpos (GLFWwindow* win, double x, double y)
{
  int wn_w, wn_h, fb_w, fb_h;
  glfwGetWindowSize(win, &wn_w, &wn_h);
  glfwGetFramebufferSize(win, &fb_w, &fb_h);
  x = x * fb_w / wn_w;
  y = (wn_h - y) * fb_h / wn_h;
  arcball->AccumulateMouseMotion((float) x, (float) y, (float) fb_w, (float) fb_h);
}
static void cursorinit (GLFWwindow* win, double x, double y)
{
  int wn_w, wn_h, fb_w, fb_h;
  glfwGetWindowSize(win, &wn_w, &wn_h);
  glfwGetFramebufferSize(win, &fb_w, &fb_h);
  x = x * fb_w / wn_w;
  y = (wn_h - y) * fb_h / wn_h;
  arcball->InitMouseMotion((float) x, (float) y);
  glfwSetCursorPosCallback(win, cursorpos);
}
static void mousebutton (GLFWwindow* win, int button, int action, int mods)
{
  (void) button; (void) mods;
  if (action == GLFW_PRESS)
    glfwSetCursorPosCallback(win, cursorinit);
  else // GLFW_RELEASE
    glfwSetCursorPosCallback(win, nullptr);
}

static void scroll (GLFWwindow* win, double xoffset, double yoffset)
{
  (void) win; (void) xoffset;
  float fov = camera->GetAngle() - 2.5f * (float) yoffset;
  fov = std::min(90.0f, std::max(10.0f, fov));
  camera->SetAngle(fov);
}

int main ()
{
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#ifdef __APPLE__
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

  glfwSetErrorCallback(error);

  GLFWwindow* win = glfwCreateWindow(600, 400, "Opcao 1: mesa com objetos", nullptr, nullptr);
  assert(win);
  glfwSetFramebufferSizeCallback(win, resize);
  glfwSetKeyCallback(win, keyboard);
  glfwSetMouseButtonCallback(win, mousebutton);
  glfwSetScrollCallback(win, scroll);

  int fbw, fbh;
  glfwGetFramebufferSize(win, &fbw, &fbh);
  ctx = WgpuContext::Make(win, fbw, fbh);
  renderer = Renderer::Make(ctx->GetDevice(), true,
                            {kBackground[0], kBackground[1], kBackground[2], kBackground[3]});

  initialize();

  while (!glfwWindowShouldClose(win)) {
    display();
    glfwPollEvents();
  }
  ctx = nullptr;
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}