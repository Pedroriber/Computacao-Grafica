// Projeto 3 (INF1761) -- Luminaria Luxor com PBR e shadow mapping.
//
//  * Base: chao + objetos iluminados por luzes pontuais (uma no alto da
//    cena, outra dentro da lampada da luminaria).
//  * Fidelidade: PBR microfacet (GGX + Smith + Schlick) em shaders/proj3/pbr.wgsl,
//    comparavel com Phong em tempo de execucao (tecla P).
//  * Extra 1: animacao da luminaria (pasta luxor/), salto no estilo Pixar.
//  * Extra 2: shadow mapping com PCF -- a luz do alto projeta a sombra da
//    luminaria (e dos objetos) no chao.
//
// Teclas:
//   Baixo / Cima            abaixar / levantar a luminaria
//   Direita / Esquerda      pular para frente / para tras (so abaixada)
//   Shift + Dir / Esq       girar a cabeca
//   P   alterna PBR <-> Phong        S   liga/desliga sombra
//   C   raio do PCF (0 = sombra dura, 1, 2, 3)
//   B   liga/desliga a luz da lampada
//   E   liga/desliga o reflexo do ceu nos metais (so PBR; para comparacao)
//   T   imprime no console ms/quadro e fps
//   Q   sair
// Mouse: arcball; roda do mouse: zoom.

#include <GLFW/glfw3.h>

#include "wgpucontext.h"
#include "arcball.h"
#include "scene.h"
#include "state.h"
#include "camera3d.h"
#include "phongmaterial.h"
#include "pointlight.h"
#include "textureset.h"
#include "texdepth.h"
#include "sampler.h"
#include "transform.h"
#include "node.h"
#include "cube.h"
#include "sphere.h"
#include "shader.h"
#include "pipeline.h"
#include "renderer.h"
#include "algorithm.h"
#include "framebuffer.h"
#include "error.h"
#include "luxor/luxor.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// ---- cena -----------------------------------------------------------------
// v-sync desligado (sem limite de 60 fps), para medir o desempenho
constexpr bool vsync = false;

constexpr double kBackground[4] = {0.10, 0.11, 0.14, 1.0};

// ---- luz do alto / mapa de sombra -----------------------------------------
constexpr int   kShadowSize = 2048;
const Vec3      kLightPos    = {40.0f, 320.0f, 60.0f};
const Vec3      kLightTarget = {0.0f, 0.0f, 40.0f};
constexpr float kLightFovDeg = 90.0f;      // frustum da luz (quadrado): cobre o chao todo
// cone do spot da lampada: intensidade cheia ate o angulo interno, zero no externo
constexpr float kBulbInnerDeg = 20.0f, kBulbOuterDeg = 42.0f;
// passo entre amostras do PCF, em texels do mapa (1 texel = ~0.3 unid. de mundo no chao);
// com 1 texel o efeito do raio ficava pequeno demais para ser visto
// bias da sombra (fixos): constante em profundidade NDC e deslocamento ao longo da normal
constexpr float kDepthBias = 0.0005f, kNormalBias = 0.6f;
// material da luminaria (fixo): plastico vermelho brilhante
constexpr float kLampMetallic = 0.0f, kLampRoughness = 0.35f;
constexpr float kPcfStepTexels = 3.0f;
constexpr float kLightNear = 150.0f, kLightFar = 700.0f;

// Camera "da luz": so fornece view/proj ao Algorithm::RenderPass.
class LightCamera : public Camera {
public:
  Mat4 GetViewMatrix () const override {
    return Mat4LookAt(kLightPos, kLightTarget, Vec3{0.0f, 0.0f, -1.0f});
  }
  Mat4 GetProjMatrix (std::pair<int, int>) const override {
    return Mat4Perspective(Radians(kLightFovDeg), 1.0f, kLightNear, kLightFar);
  }
};

// ---- estado ---------------------------------------------------------------
WgpuContextPtr ctx;
RendererPtr renderer;
AlgorithmPtr algorithm;
ScenePtr scene, shadowScene;
Camera3DPtr camera;
ArcballPtr arcball;
LuxorPtr luxor;
ShaderPtr shader;
Framebuffer shadowFb;
std::shared_ptr<LightCamera> lightCamera;
PhongMaterialPtr lampRed, lampWhite;
NodePtr casters;
TexDepthPtr shadowTex;        // precisam viver ate o fim do programa:
SamplerPtr shadowSampler;     // o Framebuffer guarda a view da textura
PipelinePtr shadowPipe;

bool  useSky = true, usePBR = true, useShadow = true, bulbOn = true, gammaManual = false;
int   pcfRadius = 1;
int   winW = 1280, winH = 720;

// ---- materiais ------------------------------------------------------------
float ShininessFromRoughness (float r) { return std::max(2.0f / (r * r) - 2.0f, 1.0f); }

void SetSurface (PhongMaterialPtr m, float r, float g, float b, float metallic, float rough,
                 float emissive = 0.0f, float checker = 0.0f)
{
  m->SetBaseColor(r, g, b);
  m->SetSpecular(0.5f, 0.5f, 0.5f);                  // so o modo Phong usa
  m->SetShininess(ShininessFromRoughness(rough));    // idem (mesma rugosidade)
  m->Set("metallic", metallic);
  m->Set("roughness", rough);
  m->Set("emissive", emissive);
  m->Set("checker", checker);
}

// ---- helpers de cena (mesma convencao das tarefas anteriores) -------------
NodePtr MakeBox (CubePtr cube, PhongMaterialPtr mat, float sx, float sy, float sz,
                 float tx, float ty, float tz, float rotY = 0.0f)
{
  TransformPtr t = Transform::Make();
  t->Translate(tx, ty, tz);   // ty = BASE do cubo
  t->Rotate(rotY, 0.0f, 1.0f, 0.0f);
  t->Scale(sx, sy, sz);
  return Node::Make(t, {mat}, {cube});
}
NodePtr MakeSphere (SpherePtr s, PhongMaterialPtr mat, float r, float tx, float ty, float tz)
{
  TransformPtr t = Transform::Make();
  t->Translate(tx, ty, tz);   // ty = CENTRO da esfera
  t->Scale(r, r, r);
  return Node::Make(t, {mat}, {s});
}

VertexBufferSpec Buf3 (const char* name)
{
  VertexBufferSpec b;
  b.arrayStride = 3 * 4;
  b.attributes.push_back({std::string(name), std::nullopt, WGPUVertexFormat_Float32x3, 0});
  return b;
}

void initialize ()
{
  WGPUDevice dev = ctx->GetDevice();
  WGPUTextureFormat fmt = ctx->GetSurfaceFormat();
  gammaManual = !(fmt == WGPUTextureFormat_BGRA8UnormSrgb || fmt == WGPUTextureFormat_RGBA8UnormSrgb);

  camera = Camera3D::Make(0.0f, 120.0f, 330.0f);
  camera->SetCenter(0.0f, 28.0f, 40.0f);
  camera->SetAngle(40.0f);
  camera->SetZPlanes(5.0f, 3000.0f);
  arcball = camera->CreateArcball();

  // luz pontual no alto, em espaco do mundo
  PointLightPtr light = PointLight::Make(kLightPos.x, kLightPos.y, kLightPos.z, "world");
  light->SetAmbient(0.10f, 0.11f, 0.14f);
  light->SetDiffuse(3.2f, 3.1f, 2.9f);    // radiancia logo abaixo da luz (PBR trabalha em HDR)
  light->SetSpecular(1.0f, 1.0f, 1.0f);

  // ---- shader PBR (+ sombra) ----
  shader = Shader::Make(dev, "../shaders/proj3/pbr.wgsl", light, "world");
  shader->SetVertexBuffers({Buf3("coord"), Buf3("normal")});
  PipelinePtr pipeline = Pipeline::Make(shader, fmt, PipelineDesc{});

  // ---- shader de profundidade (passe da luz) ----
  ShaderPtr shadowShader = Shader::Make(dev, "../shaders/proj3/shadow.wgsl", nullptr, "world");
  shadowShader->SetVertexBuffers({Buf3("coord"), Buf3("normal")});
  PipelineDesc sdesc;
  sdesc.depthStencilMode = DepthStencilMode::Explicit;
  sdesc.depthStencil.format = WGPUTextureFormat_Depth32Float;
  sdesc.depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
  sdesc.depthStencil.depthCompare = WGPUCompareFunction_Less;
  sdesc.depthStencil.depthBias = 2;
  sdesc.depthStencil.depthBiasSlopeScale = 2.0f;
  sdesc.depthStencil.stencilReadMask = 0xFFFFFFFF;
  sdesc.depthStencil.stencilWriteMask = 0xFFFFFFFF;
  PipelinePtr shadowPipeline = Pipeline::Make(shadowShader, std::nullopt, sdesc);

  // ---- mapa de sombra: textura de profundidade + sampler de comparacao ----
  shadowTex = TexDepth::Make(dev, "shadow_map", kShadowSize, kShadowSize);
  shadowSampler = Sampler::Make(dev, "shadow_sampler",
      WGPUAddressMode_ClampToEdge, WGPUAddressMode_ClampToEdge,
      WGPUFilterMode_Linear, WGPUFilterMode_Linear, WGPUMipmapFilterMode_Nearest,
      WGPUCompareFunction_Less);
  TextureSetPtr shadowSet = TextureSet::Make({shadowTex.get(), shadowSampler.get()});
  shader->AddTextureSet(shadowSet.get());

  WGPURenderPassDepthStencilAttachment depthAtt = {};
  depthAtt.view = shadowTex->GetView();
  depthAtt.depthLoadOp = WGPULoadOp_Clear;
  depthAtt.depthStoreOp = WGPUStoreOp_Store;
  depthAtt.depthClearValue = 1.0f;
  shadowFb.depthStencilAttachment = depthAtt;

  lightCamera = std::make_shared<LightCamera>();
  Mat4 lightViewProj = Mat4Multiply(lightCamera->GetProjMatrix({kShadowSize, kShadowSize}),
                                    lightCamera->GetViewMatrix());
  shader->SetValue("light_view_proj", lightViewProj);

  // ---- materiais ----
  luxor = Luxor::Make(dev);
  std::vector<PhongMaterialPtr> lm = luxor->GetMaterials();   // {vermelho, branco}
  lampRed = lm[0]; lampWhite = lm[1];
  SetSurface(lampRed, 0.85f, 0.03f, 0.03f, kLampMetallic, kLampRoughness);
  SetSurface(lampWhite, 1.0f, 0.95f, 0.8f, 0.0f, 0.4f, 6.0f);   // lampada acesa

  PhongMaterialPtr floorMat = PhongMaterial::Make(0.6f, 0.6f, 0.62f);
  SetSurface(floorMat, 0.6f, 0.6f, 0.62f, 0.0f, 0.55f, 0.0f, 1.0f);
  PhongMaterialPtr blueMat = PhongMaterial::Make(0.1f, 0.25f, 0.75f);
  SetSurface(blueMat, 0.10f, 0.25f, 0.75f, 0.0f, 0.45f);
  PhongMaterialPtr greenMat = PhongMaterial::Make(0.45f, 0.18f, 0.05f);   // esfera metalica (bronze escuro)
  SetSurface(greenMat, 0.45f, 0.18f, 0.05f, 1.0f, 0.15f);
  PhongMaterialPtr woodMat = PhongMaterial::Make(0.55f, 0.35f, 0.15f);
  SetSurface(woodMat, 0.55f, 0.35f, 0.15f, 0.0f, 0.70f);

  for (PhongMaterialPtr m : {lampRed, lampWhite, floorMat, blueMat, greenMat, woodMat})
    shader->AddMaterial(m.get());

  // ---- geometria ----
  CubePtr cube = Cube::Make(dev);
  SpherePtr sphere = Sphere::Make(dev);

  NodePtr floor = MakeBox(cube, floorMat, 500.0f, 2.0f, 500.0f, 0.0f, -2.0f, 0.0f);  // topo em y = 0
  NodePtr box1  = MakeBox(cube, blueMat, 36.0f, 36.0f, 36.0f, -90.0f, 0.0f, 40.0f, 25.0f);
  NodePtr box2  = MakeBox(cube, woodMat, 28.0f, 70.0f, 28.0f, 105.0f, 0.0f, -50.0f, -15.0f);
  NodePtr ball  = MakeSphere(sphere, greenMat, 18.0f, 95.0f, 18.0f, 30.0f);
  NodePtr lamp  = luxor->GetNode();

  // Um no so e pai dos objetos que projetam sombra (luminaria e objetos).
  // Um no so pode ter um pai, entao o passe de sombra reaproveita esse no
  // como raiz de uma segunda cena, trocando temporariamente o seu Pipeline
  // (ver display). O chao nao entra: so recebe sombra.
  casters = Node::Make({box1, box2, ball, lamp});
  NodePtr mainRoot = Node::Make(pipeline, {shadowSet}, {floor, casters});
  scene = Scene::Make(mainRoot);
  scene->AddEngine(luxor->GetEngine());
  shadowScene = Scene::Make(casters);
  shadowPipe = shadowPipeline;
}

// Valores que mudam a cada quadro (animacao da lampada, teclas).
void UpdateUniforms ()
{
  Mat4 M = luxor->GetLightNode()->GetModelMatrix();
  Vec4 bulb = Mat4MulVec4(M, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
  float refDist = kLightPos.y;            // altura da luz sobre o chao
  shader->SetValue("extra", Vec4{refDist * refDist, useSky ? 1.0f : 0.0f, 0.0f, 0.0f});
  shader->SetValue("bulb_position", bulb);
  // direcao do spot: eixo +z da cupula (a cupula abre para +z), em mundo
  Vec4 dir = Mat4MulVec4(M, Vec4{0.0f, 0.0f, 1.0f, 0.0f});
  Vec3 dn = Vec3Normalize(Vec3{dir.x, dir.y, dir.z});
  shader->SetValue("bulb_direction", Vec4{dn.x, dn.y, dn.z, 0.0f});
  shader->SetValue("bulb_cone", Vec4{std::cos(Radians(kBulbOuterDeg)), std::cos(Radians(kBulbInnerDeg)), 0.0f, 0.0f});
  shader->SetValue("bulb_color", Vec4{1.0f, 0.88f, 0.65f, bulbOn ? 12000.0f : 0.0f});
  shader->SetValue("params", Vec4{usePBR ? 1.0f : 0.0f, useShadow ? 1.0f : 0.0f,
                                  (float) pcfRadius, kDepthBias});
  shader->SetValue("params2", Vec4{gammaManual ? 1.0f : 0.0f, kNormalBias, 1.0f / kShadowSize, kPcfStepTexels});
  lampWhite->Set("emissive", bulbOn ? 6.0f : 0.0f);
}

void display (float dt)
{
  scene->Update(dt);
  UpdateUniforms();

  WGPUSurfaceTexture surfaceTex = ctx->GetCurrentTexture();
  if (surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    return;

  if (useShadow) {   // passe 1: profundidade vista da luz
    casters->SetPipeline(shadowPipe);      // so durante o passe de sombra
    algorithm->RenderPass(shadowFb, shadowScene, lightCamera, {kShadowSize, kShadowSize});
    casters->SetPipeline(nullptr);         // no passe final herda o PBR
  }
  renderer->Render(surfaceTex.texture, scene, camera);   // passe 2: cena final

  ctx->Present();
  wgpuTextureRelease(surfaceTex.texture);
}

// ---- GLFW -----------------------------------------------------------------
double accTime = 0.0; int accFrames = 0; double lastMs = 0.0, lastFps = 0.0;

void error (int code, const char* msg)
{
  printf("GLFW error %d: %s\n", code, msg);
  glfwTerminate();
  exit(0);
}

void keyboard (GLFWwindow* window, int key, int scancode, int action, int mods)
{
  (void) scancode;
  if (action == GLFW_RELEASE) return;
  bool press = action == GLFW_PRESS;
  bool shift = (mods & GLFW_MOD_SHIFT) != 0;
  LuxorEnginePtr eng = luxor->GetEngine();
  switch (key) {
    case GLFW_KEY_Q: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_DOWN:  eng->StandDown(); break;
    case GLFW_KEY_UP:    eng->StandUp(); break;
    case GLFW_KEY_RIGHT: if (shift) eng->TurnHead(-5.0f); else eng->JumpForward(); break;
    case GLFW_KEY_LEFT:  if (shift) eng->TurnHead(5.0f);  else eng->JumpBackward(); break;
    default: break;
  }
  if (!press) return;
  switch (key) {
    case GLFW_KEY_P: usePBR = !usePBR; printf("modelo: %s\n", usePBR ? "PBR" : "Phong"); break;
    case GLFW_KEY_S: useShadow = !useShadow; printf("sombra: %s\n", useShadow ? "on" : "off"); break;
    case GLFW_KEY_C: pcfRadius = (pcfRadius + 1) % 4;
                     printf("PCF raio %d (%dx%d amostras)\n", pcfRadius, 2*pcfRadius+1, 2*pcfRadius+1); break;
    case GLFW_KEY_E: useSky = !useSky; printf("reflexo do ceu (so PBR): %s\n", useSky ? "on" : "off"); break;
    case GLFW_KEY_B: bulbOn = !bulbOn; printf("lampada: %s\n", bulbOn ? "acesa" : "apagada"); break;
    case GLFW_KEY_T:
      printf("[%s, sombra %s, PCF %dx%d] %.3f ms/quadro (%.1f fps)\n",
             usePBR ? "PBR" : "Phong", useShadow ? "on" : "off", 2*pcfRadius+1, 2*pcfRadius+1,
             lastMs, lastFps);
      break;
    default: break;
  }
}

void resize (GLFWwindow*, int width, int height)
{
  if (width > 0 && height > 0) { winW = width; winH = height; ctx->Configure(width, height, vsync); }
}

void cursorpos (GLFWwindow* win, double x, double y)
{
  int wn_w, wn_h, fb_w, fb_h;
  glfwGetWindowSize(win, &wn_w, &wn_h);
  glfwGetFramebufferSize(win, &fb_w, &fb_h);
  x = x * fb_w / wn_w;
  y = (wn_h - y) * fb_h / wn_h;
  arcball->AccumulateMouseMotion((float) x, (float) y, (float) fb_w, (float) fb_h);
}
void cursorinit (GLFWwindow* win, double x, double y)
{
  int wn_w, wn_h, fb_w, fb_h;
  glfwGetWindowSize(win, &wn_w, &wn_h);
  glfwGetFramebufferSize(win, &fb_w, &fb_h);
  x = x * fb_w / wn_w;
  y = (wn_h - y) * fb_h / wn_h;
  arcball->InitMouseMotion((float) x, (float) y);
  glfwSetCursorPosCallback(win, cursorpos);
}
void mousebutton (GLFWwindow* win, int button, int action, int mods)
{
  (void) button; (void) mods;
  if (action == GLFW_PRESS) glfwSetCursorPosCallback(win, cursorinit);
  else glfwSetCursorPosCallback(win, nullptr);
}
void scroll (GLFWwindow*, double, double yoffset)
{
  float fov = camera->GetAngle() - 2.5f * (float) yoffset;
  camera->SetAngle(std::min(90.0f, std::max(10.0f, fov)));
}

} // namespace

int main ()
{
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwSetErrorCallback(error);

  GLFWwindow* win = glfwCreateWindow(winW, winH, "Projeto 3: Luxor", nullptr, nullptr);
  assert(win);
  glfwSetFramebufferSizeCallback(win, resize);
  glfwSetKeyCallback(win, keyboard);
  glfwSetMouseButtonCallback(win, mousebutton);
  glfwSetScrollCallback(win, scroll);

  int fbw, fbh;
  glfwGetFramebufferSize(win, &fbw, &fbh);
  winW = fbw; winH = fbh;
  ctx = WgpuContext::Make(win, fbw, fbh);
  ctx->Configure(fbw, fbh, vsync);   // v-sync desligado
  renderer = Renderer::Make(ctx->GetDevice(), true,
                            {kBackground[0], kBackground[1], kBackground[2], kBackground[3]});
  algorithm = Algorithm::Make(ctx->GetDevice());

  initialize();

  using Clock = std::chrono::steady_clock;
  Clock::time_point last = Clock::now();
  while (!glfwWindowShouldClose(win)) {
    Clock::time_point now = Clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    dt = std::min(dt, 0.05f);                 // evita saltos apos travadas

    display(dt);
    glfwPollEvents();

    accTime += std::chrono::duration<double>(Clock::now() - now).count();
    accFrames++;
    if (accTime >= 0.5) {
      lastMs = 1000.0 * accTime / accFrames;
      lastFps = accFrames / accTime;
      char title[160];
      snprintf(title, sizeof(title), "Projeto 3: Luxor | %s | sombra %s PCF%d | %.2f ms (%.0f fps)",
               usePBR ? "PBR" : "Phong", useShadow ? "on" : "off", pcfRadius, lastMs, lastFps);
      glfwSetWindowTitle(win, title);
      accTime = 0.0; accFrames = 0;
    }
  }
  shadowFb = Framebuffer{};
  scene = nullptr; shadowScene = nullptr; casters = nullptr;
  luxor = nullptr; shadowTex = nullptr; shadowSampler = nullptr;
  shader = nullptr; shadowPipe = nullptr;
  algorithm = nullptr; renderer = nullptr;
  ctx = nullptr;
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}