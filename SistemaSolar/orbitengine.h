#ifndef ORBITENGINE_H
#define ORBITENGINE_H

#include "engine.h"
#include "transform.h"
#include <memory>

// Anima uma orbita circular girando continuamente um Transform "pivo" em
// torno do eixo Z -- exatamente o mesmo padrao usado no MovePointer do
// exemplo do professor (Rotate() chamado a cada Update, acumulando rotacao
// quadro a quadro).
//
// A orbita em si nao e calculada aqui com seno/cosseno: ela emerge da
// composicao de transforms do grafo de cena. O Transform controlado por
// este Engine (o "pivo") fica na raiz da subarvore do corpo orbitante; um
// no filho aplica uma translacao FIXA (nao animada) de distancia igual ao
// raio da orbita. Como o pivo gira continuamente, essa translacao fixa
// "varre" um circulo -- e assim tambem se comporta qualquer sub-orbita
// aninhada dentro dele (por isso a Lua, cujo pivo fica dentro da subarvore
// da Terra, orbita a Terra que por sua vez orbita o Sol).
class OrbitEngine;
using OrbitEnginePtr = std::shared_ptr<OrbitEngine>;

class OrbitEngine : public Engine
{
  TransformPtr m_pivot;
  float m_angularSpeed; // radianos por segundo

protected:
  OrbitEngine (TransformPtr pivot, float angularSpeed);

public:
  static OrbitEnginePtr Make (TransformPtr pivot, float angularSpeed);

  void Update (float dt) override;
};

#endif
