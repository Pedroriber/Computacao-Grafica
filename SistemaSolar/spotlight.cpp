#include "spotlight.h"
#include <cmath>

static const float kPi = 3.14159265358979323846f;

SpotLight::SpotLight (float px, float py, float pz, float dx, float dy, float dz,
                      float apertureDegrees, float exponent, const std::string& space)
  : Light(space)
{
  SetPosition(px, py, pz);
  SetDirection(dx, dy, dz);
  SetAperture(apertureDegrees);
  SetExponent(exponent);
}

SpotLightPtr SpotLight::Make (float px, float py, float pz,
                              float dx, float dy, float dz,
                              float apertureDegrees, float exponent,
                              const std::string& space)
{
  return SpotLightPtr(new SpotLight(px, py, pz, dx, dy, dz, apertureDegrees, exponent, space));
}

void SpotLight::SetPosition (float x, float y, float z) { Set("light_position", Vec4{x, y, z, 1.0f}); }
void SpotLight::SetDirection (float x, float y, float z) { Set("light_direction", Vec3{x, y, z}); }

// o shader compara c = -l . s_dir diretamente com cos(theta), entao o
// cosseno e calculado uma unica vez aqui, em vez de por fragmento
void SpotLight::SetAperture (float degrees)
{
  Set("light_spot_cos_cutoff", std::cos(degrees * kPi / 180.0f));
}

void SpotLight::SetExponent (float exponent) { Set("light_spot_exponent", exponent); }

// So os valores geometricos passam pelo mapeamento de espaco: a posicao
// (ponto, w = 1: sofre rotacao e translacao) e a direcao (vetor, w = 0:
// so rotacao, depois normalizada). Abertura e expoente nao dependem de
// espaco e seguem direto de m_values.
std::map<std::string, UniformValue> SpotLight::Map (const Mat4& matrix) const
{
  Vec4 position = std::get<Vec4>(Get("light_position", Vec4{0, 0, 0, 1}));
  Vec3 d = std::get<Vec3>(Get("light_direction", Vec3{0, 0, -1}));
  Vec4 mappedDir = Mat4MulVec4(matrix, Vec4{d.x, d.y, d.z, 0.0f});
  Vec3 direction = Vec3Normalize(Vec3{mappedDir.x, mappedDir.y, mappedDir.z});
  return {
    {"light_position", Mat4MulVec4(matrix, position)},
    {"light_direction", direction},
  };
}
