#include <memory>
class SpotLight;
using SpotLightPtr = std::shared_ptr<SpotLight>;

#ifndef SPOTLIGHT_H
#define SPOTLIGHT_H

#include "light.h"

// Luz de spot: posicao + direcao + angulo de abertura (theta) + expoente
// de concentracao (e_pos), alem da cor herdada de Light (ambient/diffuse/
// specular).
//
// Campos enviados ao shader (o shader precisa declarar, no bloco "global",
// os campos que quiser receber -- Light::WriteFields so escreve os que
// existem):
//   light_position        vec4  posicao da fonte (w = 1), mapeada p/ o espaco
//                               de iluminacao do shader, como em PointLight
//   light_direction       vec3  direcao do spot, mapeada (w = 0) e
//                               normalizada, como em DirectionalLight
//   light_spot_cos_cutoff f32   cos(theta): o shader testa c >= cos(theta)
//   light_spot_exponent   f32   expoente s_exp usado em max(c,0)^s_exp
//
// O coeficiente k_spot em si NAO e calculado aqui: depende do ponto
// iluminado, entao e calculado por fragmento, no shader.
class SpotLight : public Light {
protected:
  SpotLight (float px, float py, float pz, float dx, float dy, float dz,
             float apertureDegrees, float exponent, const std::string& space);

public:
  static SpotLightPtr Make (float px, float py, float pz,
                            float dx, float dy, float dz,
                            float apertureDegrees, float exponent,
                            const std::string& space = "world");

  void SetPosition (float x, float y, float z);
  void SetDirection (float x, float y, float z);
  // angulo de abertura theta, em graus (metade do angulo total do cone,
  // medido a partir da direcao central)
  void SetAperture (float degrees);
  void SetExponent (float exponent);

  std::map<std::string, UniformValue> Map (const Mat4& matrix) const override;
};

#endif
