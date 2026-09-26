// solarsystem.cpp: Mini-sistema solar (Prof. Waldemar Celes, PUC-Rio)
//
// Vista superior do movimento da Terra em torno do Sol e da Lua em torno
// da Terra, usando o grafo de cena fornecido pelo professor. Alem disso:
//  - Rotacao da Terra em torno do proprio eixo (a Lua NAO herda esse giro)
//  - Venus, orbitando o Sol entre ele e a Terra
//  - Textura de fundo representando o espaco
//
// Hierarquia da cena (cada seta = relacao pai -> filho no grafo):
//
//   root (pipeline)
//    |- backgroundNode                (quad grande com a textura do espaco,
//    |                                 desenhado primeiro -> fica atras de
//    |                                 tudo, ja que sem depth test a ordem
//    |                                 de desenho e que decide o empilhamento)
//    |- sunNode                       (fixo no centro, disco maior)
//    |- venusPivot                    (Transform que gira -> orbita o Sol)
//    |   |- venusOffset               (Transform fixo: distancia ao Sol)
//    |       |- venusDiskNode         (disco de Venus, escalado)
//    |- earthPivot                    (Transform que gira -> orbita o Sol)
//        |- earthOffset               (Transform fixo: distancia ao Sol)
//            |- earthSpinPivot        (Transform que gira -> rotacao propria)
//            |   |- earthDiskNode     (disco da Terra, escalado)
//            |- moonPivot             (Transform que gira -> orbita a Terra)
//                |- moonOffset        (Transform fixo: distancia a Terra)
//                    |- moonDiskNode  (disco da Lua, escalado)
//
// Como moonPivot esta aninhado DENTRO da subarvore da Terra (filho de
// earthOffset), ele herda a posicao orbital da Terra automaticamente --
// a Lua orbita a Terra, que por sua vez orbita o Sol, sem nenhum calculo
// manual de posicao no codigo da aplicacao.
//
// IMPORTANTE: earthSpinPivot (rotacao propria da Terra) e moonPivot
// (orbita da Lua) sao IRMAOS dentro de earthOffset, nao um filho do
// outro -- por isso a Lua acompanha a TRANSLACAO da Terra (esta dentro de
// earthOffset), mas nao herda sua ROTACAO PROPRIA (nao esta dentro de
// earthSpinPivot). E exatamente essa separacao de ramos que garante o
// requisito "a Lua nao herda a rotacao da Terra".
//
// Todos os astros (Sol, Venus, Terra, Lua) e o fundo sao texturizados via
// TextureSet (Texture + Sampler), usando o shader dedicado
// texture_shader.wgsl (grupo 1 = texture_2d + sampler, no lugar do grupo
// "material" de cor solida do shader.wgsl original).

#include <GLFW/glfw3.h>

#include "wgpucontext.h"
#include "scene.h"
#include "state.h"
#include "camera2d.h"
#include "transform.h"
#include "disk.h"
#include "quad.h"
#include "texture.h"
#include "sampler.h"
#include "textureset.h"
#include "node.h"
#include "shader.h"
#include "pipeline.h"
#include "renderer.h"
#include "engine.h"
#include "orbitengine.h"
#include "error.h"

#include <cassert>
#include <cstdio>

static WgpuContextPtr ctx;
static RendererPtr renderer;
static ScenePtr scene;
static Camera2DPtr camera;

// Parametros artisticos do sistema (nao sao escalas astronomicas reais --
// se fossem, a orbita da Terra nao caberia na tela junto com um Sol
// visivel, e a Lua seria praticamente invisivel). Velocidades escolhidas
// livremente, mas respeitando a ordem relativa real (quanto mais perto do
// Sol, mais rapida a translacao).
namespace {
constexpr float kSunScale = 2.5f;

constexpr float kVenusOrbitRadius = 4.0f;    // entre o Sol e a Terra
constexpr float kVenusScale = 0.75f;
constexpr float kVenusAngularSpeed = 1.9f;   // rad/s (mais rapido que a Terra, mais perto do Sol)

constexpr float kEarthOrbitRadius = 6.0f;
constexpr float kEarthScale = 0.9f;
constexpr float kEarthOrbitAngularSpeed = 1.2f;  // rad/s (translacao em torno do Sol)
constexpr float kEarthSpinAngularSpeed = 5.0f;   // rad/s (rotacao em torno do proprio eixo)

constexpr float kMoonOrbitRadius = 1.6f;
constexpr float kMoonScale = 0.35f;
constexpr float kMoonAngularSpeed = 3.8f;    // rad/s

// caminho local das texturas, como informado
const char* kImagesDir = R"(C:\Users\pribe\Documents\Estudos-Computacao-Grafica\images\)";
} // namespace

static void initialize (void)
{
  camera = Camera2D::Make(-12.0f, 12.0f, -12.0f, 12.0f);

  DiskPtr disk = Disk::Make(ctx->GetDevice());

  // um sampler compartilhado (mesma regra de filtragem/enderecamento para
  // todas as texturas) e uma Texture por astro/fundo, carregada direto do
  // arquivo. Os varnames ("tex", "samp") precisam bater com os nomes
  // declarados em texture_shader.wgsl.
  SamplerPtr sampler = Sampler::Make(ctx->GetDevice(), "samp");
  TexturePtr sunTexture = Texture::Make(ctx->GetDevice(), "tex", std::string(kImagesDir) + "sun.jpg");
  TexturePtr venusTexture = Texture::Make(ctx->GetDevice(), "tex", std::string(kImagesDir) + "venus.jpg");
  TexturePtr earthTexture = Texture::Make(ctx->GetDevice(), "tex", std::string(kImagesDir) + "earth.jpg");
  TexturePtr moonTexture = Texture::Make(ctx->GetDevice(), "tex", std::string(kImagesDir) + "moon.jpg");
  TexturePtr spaceTexture = Texture::Make(ctx->GetDevice(), "tex", std::string(kImagesDir) + "space.jpg");

  TextureSetPtr sunTextureSet = TextureSet::Make({sunTexture.get(), sampler.get()});
  TextureSetPtr venusTextureSet = TextureSet::Make({venusTexture.get(), sampler.get()});
  TextureSetPtr earthTextureSet = TextureSet::Make({earthTexture.get(), sampler.get()});
  TextureSetPtr moonTextureSet = TextureSet::Make({moonTexture.get(), sampler.get()});
  TextureSetPtr spaceTextureSet = TextureSet::Make({spaceTexture.get(), sampler.get()});

  // --- Fundo: quad grande cobrindo toda a area visivel da camera ---
  // quad.h confirma que o Quad base vai de [0,1]x[0,1] (nao e centrado na
  // origem). A PRIMEIRA chamada de Transform vira a operacao mais externa
  // (aplicada por ultimo -- posicionamento no mundo) e a SEGUNDA vira a
  // mais interna (aplicada primeiro -- escala local), pelo mesmo padrao
  // do exemplo original do professor (Translate antes de Scale). Por
  // isso Translate precisa vir ANTES de Scale aqui: o quad e escalado
  // para 30x30 em coordenadas locais e so DEPOIS deslocado -15 em x e y
  // para ficar centralizado na camera (que vai de -12 a 12).
  TransformPtr backgroundTrf = Transform::Make();
  backgroundTrf->Translate(-15.0f, -15.0f, 0.0f);
  backgroundTrf->Scale(30.0f, 30.0f, 1.0f);
  QuadPtr backgroundQuad = Quad::Make(ctx->GetDevice());
  NodePtr backgroundNode = Node::Make(backgroundTrf, {spaceTextureSet}, {backgroundQuad});

  // --- Sol: fixo no centro ---
  TransformPtr sunTrf = Transform::Make();
  sunTrf->Scale(kSunScale, kSunScale, 1.0f);
  NodePtr sunNode = Node::Make(sunTrf, {sunTextureSet}, {disk});

  // --- Venus: pivo que orbita o Sol (so translacao, sem rotacao propria) ---
  TransformPtr venusPivotTrf = Transform::Make();
  TransformPtr venusOffsetTrf = Transform::Make();
  venusOffsetTrf->Translate(kVenusOrbitRadius, 0.0f, 0.0f);
  TransformPtr venusDiskTrf = Transform::Make();
  venusDiskTrf->Scale(kVenusScale, kVenusScale, 1.0f);
  NodePtr venusDiskNode = Node::Make(venusDiskTrf, {venusTextureSet}, {disk});
  NodePtr venusOffsetNode = Node::Make(venusOffsetTrf, {venusDiskNode});
  NodePtr venusPivotNode = Node::Make(venusPivotTrf, {venusOffsetNode});

  // --- Terra: pivo que orbita o Sol ---
  TransformPtr earthPivotTrf = Transform::Make();
  TransformPtr earthOffsetTrf = Transform::Make();
  earthOffsetTrf->Translate(kEarthOrbitRadius, 0.0f, 0.0f);

  // rotacao propria da Terra: um pivo a mais, filho de earthOffset e PAI
  // apenas do disco da Terra (a Lua fica fora desse ramo -- ver nota no
  // topo do arquivo).
  TransformPtr earthSpinTrf = Transform::Make();
  TransformPtr earthDiskTrf = Transform::Make();
  earthDiskTrf->Scale(kEarthScale, kEarthScale, 1.0f);
  NodePtr earthDiskNode = Node::Make(earthDiskTrf, {earthTextureSet}, {disk});
  NodePtr earthSpinNode = Node::Make(earthSpinTrf, {earthDiskNode});

  // --- Lua: pivo aninhado dentro da subarvore da Terra, orbita a Terra ---
  TransformPtr moonPivotTrf = Transform::Make();
  TransformPtr moonOffsetTrf = Transform::Make();
  moonOffsetTrf->Translate(kMoonOrbitRadius, 0.0f, 0.0f);
  TransformPtr moonDiskTrf = Transform::Make();
  moonDiskTrf->Scale(kMoonScale, kMoonScale, 1.0f);
  NodePtr moonDiskNode = Node::Make(moonDiskTrf, {moonTextureSet}, {disk});
  NodePtr moonOffsetNode = Node::Make(moonOffsetTrf, {moonDiskNode});
  NodePtr moonPivotNode = Node::Make(moonPivotTrf, {moonOffsetNode});

  // earthSpinNode e moonPivotNode sao IRMAOS aqui -- a Lua so herda a
  // translacao (esta dentro de earthOffset), nao a rotacao propria (nao
  // esta dentro de earthSpinNode).
  NodePtr earthOffsetNode = Node::Make(earthOffsetTrf, {earthSpinNode, moonPivotNode});
  NodePtr earthPivotNode = Node::Make(earthPivotTrf, {earthOffsetNode});

  ShaderPtr shader = Shader::Make(ctx->GetDevice(), "texture_shader.wgsl");

  // Dois buffers separados, igual ao padrao de Quad (quad.cpp): posicao no
  // slot 0 (location "pos") e coordenada de textura no slot 1 (location
  // "uv", que texture_shader.wgsl declara em @location(3), casando com a
  // convencao Shape::TEXCOORD de shape.h).
  VertexBufferSpec posBuf;
  posBuf.arrayStride = 2 * sizeof(float);
  posBuf.attributes.push_back({std::string("pos"), std::nullopt, WGPUVertexFormat_Float32x2, 0});

  VertexBufferSpec texcoordBuf;
  texcoordBuf.arrayStride = 2 * sizeof(float);
  texcoordBuf.attributes.push_back({std::string("uv"), std::nullopt, WGPUVertexFormat_Float32x2, 0});

  shader->SetVertexBuffers({posBuf, texcoordBuf});

  PipelineDesc pdesc;
  pdesc.depthStencilMode = DepthStencilMode::None;
  PipelinePtr pipeline = Pipeline::Make(shader, ctx->GetSurfaceFormat(), pdesc);

  // texture_shader.wgsl nao declara grupo "material" -- so grupo de
  // textura, registrado via AddTextureSet, um por astro/fundo.
  shader->AddTextureSet(spaceTextureSet.get());
  shader->AddTextureSet(sunTextureSet.get());
  shader->AddTextureSet(venusTextureSet.get());
  shader->AddTextureSet(earthTextureSet.get());
  shader->AddTextureSet(moonTextureSet.get());

  // build scene -- backgroundNode primeiro na lista: sem depth test, e a
  // ORDEM DE DESENHO que decide o empilhamento visual (pintor's
  // algorithm), entao o fundo precisa ser desenhado antes de tudo.
  NodePtr root = Node::Make(pipeline, {backgroundNode, sunNode, venusPivotNode, earthPivotNode});
  scene = Scene::Make(root);
  scene->AddEngine(OrbitEngine::Make(venusPivotTrf, kVenusAngularSpeed));
  scene->AddEngine(OrbitEngine::Make(earthPivotTrf, kEarthOrbitAngularSpeed));
  scene->AddEngine(OrbitEngine::Make(earthSpinTrf, kEarthSpinAngularSpeed));
  scene->AddEngine(OrbitEngine::Make(moonPivotTrf, kMoonAngularSpeed));
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

static void update (float dt)
{
  scene->Update(dt);
}

int main ()
{
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#ifdef __APPLE__
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

  glfwSetErrorCallback(error);

  GLFWwindow* win = glfwCreateWindow(600, 600, "Mini sistema solar", nullptr, nullptr);
  assert(win);
  glfwSetFramebufferSizeCallback(win, resize);
  glfwSetKeyCallback(win, keyboard);

  int fbw, fbh;
  glfwGetFramebufferSize(win, &fbw, &fbh);
  ctx = WgpuContext::Make(win, fbw, fbh);
  // fundo preto de base (a textura do espaco fica por cima; so importa se
  // a textura tiver algum canal transparente, o que nao e o caso de um jpg)
  renderer = Renderer::Make(ctx->GetDevice(), false, {0.0, 0.0, 0.0, 1.0});

  initialize();

  float t0 = (float) glfwGetTime();
  while (!glfwWindowShouldClose(win)) {
    float t = (float) glfwGetTime();
    update(t - t0);
    t0 = t;
    display();
    glfwPollEvents();
  }
  ctx = nullptr;
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;

}