// Cena com mesa, caixa e esferas, iluminada por spot e texturas decal.
// O projetor declarado em mesa1.wgsl permanece desativado.

#include <GLFW/glfw3.h>

#include "wgpucontext.h"
#include "arcball.h"
#include "scene.h"
#include "state.h"
#include "camera3d.h"
#include "phongmaterial.h"
#include "spotlight.h"
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

#include <cassert>
#include <cstdio>
#include <string>

static float viewer_pos[3] = {2.5f, 4.5f, 3.0f};

static WgpuContextPtr ctx;
static RendererPtr renderer;
static ScenePtr scene;
static Camera3DPtr camera;
static ArcballPtr arcball;

namespace {
// Diretório local das texturas.
const char* kTexturesDir = R"(C:\Users\pribe\Documents\Estudos-Computacao-Grafica\textures\)";

constexpr float kTableSurfaceY = 1.0f;
constexpr float kTableThickness = 0.2f;
constexpr float kLegHeight = kTableSurfaceY - kTableThickness;
constexpr float kLegInsetX = 1.0f;
constexpr float kLegInsetZ = 0.7f;
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

  // Luz de spot fixa no mundo, apontada para os objetos sobre a mesa.
  SpotLightPtr light = SpotLight::Make(0.5f, 3.5f, 2.0f,
                                       -0.4f, -2.3f, -2.0f,
                                       22.0f, 20.0f, "world");
  light->SetAmbient(0.12f, 0.12f, 0.12f);
  light->SetDiffuse(1.0f, 1.0f, 0.95f);
  light->SetSpecular(1.0f, 1.0f, 1.0f);

  // Os decals fornecem a cor dos objetos.
  PhongMaterialPtr white = PhongMaterial::Make(1.0f, 1.0f, 1.0f);

  // Texturas decal e recurso reserva do projetor.
  std::string dir(kTexturesDir);
  SamplerPtr sampler = Sampler::Make(ctx->GetDevice(), "decal_sampler");
  TexturePtr woodTex  = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "wood3d.jpg");
  TexturePtr stoneTex = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "stone3d.png");
  TexturePtr earthTex = Texture::Make(ctx->GetDevice(), "decal_texture", dir + "earth3d.jpg");
  TexturePtr whiteTex = Texture::Make(ctx->GetDevice(), "decal_texture", Vec3{1.0f, 1.0f, 1.0f});
  TexturePtr slideTex = Texture::Make(ctx->GetDevice(), "slide_texture", Vec3{1.0f, 1.0f, 1.0f});

  TextureSetPtr woodSet  = TextureSet::Make({woodTex.get(), sampler.get(), slideTex.get()});
  TextureSetPtr stoneSet = TextureSet::Make({stoneTex.get(), sampler.get(), slideTex.get()});
  TextureSetPtr earthSet = TextureSet::Make({earthTex.get(), sampler.get(), slideTex.get()});
  TextureSetPtr whiteSet = TextureSet::Make({whiteTex.get(), sampler.get(), slideTex.get()});

  CubePtr cube = Cube::Make(ctx->GetDevice());
  SpherePtr sphere = Sphere::Make(ctx->GetDevice());

  ShaderPtr shader = Shader::Make(ctx->GetDevice(), "../shaders/ilum_frag/mesa1.wgsl", light, "world");
  shader->SetValue("light2_position", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
  shader->SetValue("light2_color", Vec4{0.0f, 0.0f, 0.0f, 0.0f});
  shader->SetValue("fog_color", Vec4{0.70f, 0.75f, 0.80f, 1.0f});
  shader->SetValue("fog_params", Vec4{5.0f, 10.0f, 0.0f, 0.0f});
  shader->SetValue("clip_plane", Vec4{0.0f, 1.0f, 0.0f, 0.0f});
  shader->SetValue("effects", Vec4{0.0f, 0.0f, 0.0f, 0.0f});
  shader->SetValue("proj_position", Vec4{0.9f, 3.0f, 0.55f, 1.0f});
  shader->SetValue("proj_right", Vec4{1.0f, 0.0f, 0.0f, 0.0f});
  shader->SetValue("proj_up", Vec4{0.0f, 0.0f, -1.0f, 0.0f});
  shader->SetValue("proj_forward", Vec4{0.0f, -1.0f, 0.0f, 0.0f});
  shader->SetValue("proj_params", Vec4{1.0f / 0.14f, 0.0f, 0.0f, 0.0f});

  // Mantém os slots de vértice na ordem esperada pelas malhas.
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
  shader->AddTextureSet(whiteSet.get());

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
  float greenRadius = 0.3f;
  NodePtr earthSphere = MakeSphere(sphere, {white, earthSet}, greenRadius,
                                   -0.3f, boxTopY + greenRadius, 0.0f);

  float ballRadius = 0.4f;
  NodePtr ballSphere = MakeSphere(sphere, {white, whiteSet}, ballRadius,
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

  int fbw, fbh;
  glfwGetFramebufferSize(win, &fbw, &fbh);
  ctx = WgpuContext::Make(win, fbw, fbh);
  renderer = Renderer::Make(ctx->GetDevice(), true, {1.0, 1.0, 1.0, 1.0});

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
