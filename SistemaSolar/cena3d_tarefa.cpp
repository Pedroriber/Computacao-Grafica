// cena3d_tarefa.cpp: mesa com uma caixa e duas esferas, inspirada na ilustracao
// do professor. Iluminacao por fragmento (shader lit.wgsl) com uma fonte
// de luz pontual, e manipulador arcball acoplado a camera.
//
// Convencoes reais de geometria (confirmadas em cube.h/sphere.h):
//  - Cube: x,z centrados em [-0.5,0.5], mas y vai de 0 (base) a 1 (topo)
//    -- ou seja, translate_y de um cubo e a posicao da sua BASE, nao do
//    centro. Escalar por sy da altura sy ao cubo (base em translate_y,
//    topo em translate_y+sy).
//  - Sphere: esfera unitaria de RAIO 1 centrada na origem -- escalar por S
//    (uniformemente) da uma esfera de raio S (diametro 2S), nao raio S/2.
//
// Essas duas convencoes sao usadas para empilhar mesa -> caixa -> esfera
// sempre a partir da "superficie de apoio" de cada objeto, em vez de
// medir a partir do centro.

#include <GLFW/glfw3.h>

#include "wgpucontext.h"
#include "arcball.h"
#include "scene.h"
#include "state.h"
#include "camera3d.h"
#include "phongmaterial.h"
#include "pointlight.h"
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

static float viewer_pos[3] = {2.5f, 2.5f, 4.0f};

static WgpuContextPtr ctx;
static RendererPtr renderer;
static ScenePtr scene;
static Camera3DPtr camera;
static ArcballPtr arcball;

// Dimensoes artisticas da mesa (livre escolha, como no sistema solar --
// so precisam ser consistentes entre si para a mesa parecer uma mesa).
namespace {
constexpr float kTableSurfaceY = 1.0f;   // altura do TOPO do tampo
constexpr float kTableThickness = 0.2f;
constexpr float kLegHeight = kTableSurfaceY - kTableThickness; // chao ate a base do tampo
constexpr float kLegInsetX = 1.0f;
constexpr float kLegInsetZ = 0.7f;
} // namespace

// tx,ty,tz = posicao da BASE do cubo (nao do centro -- ver nota no topo
// do arquivo); sx,sy,sz = tamanho em cada eixo.
static NodePtr MakeBox (CubePtr cube, PhongMaterialPtr material, float sx, float sy, float sz, float tx, float ty, float tz)
{
  TransformPtr trf = Transform::Make();
  trf->Translate(tx, ty, tz);
  trf->Scale(sx, sy, sz);
  return Node::Make(trf, {material}, {cube});
}

// ty = altura do CENTRO da esfera; radius = raio real da esfera (a escala
// passada ao Transform e igual ao raio, ja que a esfera base tem raio 1).
static NodePtr MakeSphere (SpherePtr sphere, PhongMaterialPtr material, float radius, float tx, float ty, float tz)
{
  TransformPtr trf = Transform::Make();
  trf->Translate(tx, ty, tz);
  trf->Scale(radius, radius, radius);
  return Node::Make(trf, {material}, {sphere});
}

static void initialize (void)
{
  camera = Camera3D::Make(viewer_pos[0], viewer_pos[1], viewer_pos[2]);
  arcball = camera->CreateArcball();

  PointLightPtr light = PointLight::Make(0.0f, 0.0f, 0.0f, "camera");

  PhongMaterialPtr tableMaterial = PhongMaterial::Make(0.92f, 0.92f, 0.92f);
  PhongMaterialPtr boxMaterial = PhongMaterial::Make(0.85f, 0.65f, 0.13f);
  PhongMaterialPtr greenMaterial = PhongMaterial::Make(0.55f, 0.85f, 0.55f);
  PhongMaterialPtr redMaterial = PhongMaterial::Make(0.95f, 0.45f, 0.45f);

  CubePtr cube = Cube::Make(ctx->GetDevice());
  SpherePtr sphere = Sphere::Make(ctx->GetDevice());

  ShaderPtr shader = Shader::Make(ctx->GetDevice(), "../shaders/ilum_frag/lit.wgsl", light, "world");
  VertexBufferSpec coordBuf, normalBuf;
  coordBuf.arrayStride = 3 * 4;
  coordBuf.attributes.push_back({std::string("coord"), std::nullopt, WGPUVertexFormat_Float32x3, 0});
  normalBuf.arrayStride = 3 * 4;
  normalBuf.attributes.push_back({std::string("normal"), std::nullopt, WGPUVertexFormat_Float32x3, 0});
  shader->SetVertexBuffers({coordBuf, normalBuf});

  PipelineDesc pdesc;
  WGPUPrimitiveState primitive = {};
  primitive.cullMode = WGPUCullMode_Back;
  pdesc.primitive = primitive;
  PipelinePtr pipeline = Pipeline::Make(shader, ctx->GetSurfaceFormat(), pdesc);

  shader->AddMaterial(tableMaterial.get());
  shader->AddMaterial(boxMaterial.get());
  shader->AddMaterial(greenMaterial.get());
  shader->AddMaterial(redMaterial.get());

  // --- Mesa: tampo + 4 pernas + um pequeno suporte central (como na
  // ilustracao, um toco curto perto da borda da frente) ---
  // tampo: base em (surface - thickness), topo em "surface"
  NodePtr tableTop = MakeBox(cube, tableMaterial, 2.4f, kTableThickness, 1.8f,
                              0.0f, kTableSurfaceY - kTableThickness, 0.0f);

  // pernas: base no chao (y=0), topo exatamente na base do tampo
  NodePtr legFrontLeft  = MakeBox(cube, tableMaterial, 0.15f, kLegHeight, 0.15f,
                                    -kLegInsetX, 0.0f, kLegInsetZ);
  NodePtr legFrontRight = MakeBox(cube, tableMaterial, 0.15f, kLegHeight, 0.15f,
                                    kLegInsetX, 0.0f, kLegInsetZ);
  NodePtr legBackLeft   = MakeBox(cube, tableMaterial, 0.15f, kLegHeight, 0.15f,
                                    -kLegInsetX, 0.0f, -kLegInsetZ);
  NodePtr legBackRight  = MakeBox(cube, tableMaterial, 0.15f, kLegHeight, 0.15f,
                                    kLegInsetX, 0.0f, -kLegInsetZ);

  
  // --- Caixa sobre a mesa: base exatamente na superficie do tampo ---
  float boxHeight = 0.5f;
  NodePtr box = MakeBox(cube, boxMaterial, 0.8f, boxHeight, 0.8f,
                         -0.3f, kTableSurfaceY, 0.0f);
  float boxTopY = kTableSurfaceY + boxHeight;

  // --- Esfera verde em cima da caixa: centro = topo da caixa + raio ---
  float greenRadius = 0.3f;
  NodePtr greenSphere = MakeSphere(sphere, greenMaterial, greenRadius,
                                    -0.3f, boxTopY + greenRadius, 0.0f);

  // --- Esfera vermelha, maior, ao lado da caixa: centro = superficie da
  // mesa + raio ---
  float redRadius = 0.4f;
  NodePtr redSphere = MakeSphere(sphere, redMaterial, redRadius,
                                  0.5f, kTableSurfaceY + redRadius, -0.1f);

  // build scene
  NodePtr root = Node::Make(pipeline, {
    tableTop, legFrontLeft, legFrontRight, legBackLeft, legBackRight,
    box, greenSphere, redSphere,
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
  // convert screen pos (upside down) to framebuffer pos (e.g., retina displays)
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

  GLFWwindow* win = glfwCreateWindow(600, 400, "Mesa com caixa e esferas", nullptr, nullptr);
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