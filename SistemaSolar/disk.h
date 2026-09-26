#ifndef DISK_H
#define DISK_H

#include "shape.h"
#include "state.h"
#include <wgpu.h>
#include <memory>

// Disco (circulo), leque de triangulos indexado em coordenadas polares,
// seguindo exatamente o padrao de Quad (quad.cpp): dois buffers de vertice
// separados -- posicao no slot 0 (location "pos", lido por shader.wgsl) e
// coordenada de textura no slot 1 (location Shape::TEXCOORD, reservado
// para um shader com textura no futuro) -- mais um buffer de indices
// (uint32_t). Draw() commita a matriz do no via
// st->GetShader()->CommitMatrix(st), obtendo o firstInstance usado no
// draw indexado (é assim que o vertex shader sabe qual linha do storage
// buffer de matrizes usar via @builtin(instance_index)).

class Disk;
using DiskPtr = std::shared_ptr<Disk>;

class Disk : public Shape
{
  WGPUBuffer m_coordVbo = nullptr;
  WGPUBuffer m_texcoordVbo = nullptr;
  WGPUBuffer m_ibo = nullptr;
  uint32_t m_nind = 0;

protected:
  Disk (WGPUDevice device, int segments = 48);

public:
  static DiskPtr Make (WGPUDevice device, int segments = 48);
  ~Disk () override;

  void Draw (StatePtr st) override;
};

#endif
