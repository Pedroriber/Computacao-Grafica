#include "disk.h"
#include "shader.h"

#include <cmath>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// mesmo helper (e mesmo padrao de criacao de buffer via
// wgpuQueueWriteBuffer) usado em quad.cpp -- cada Shape parece duplicar
// essa funcao local, entao repito aqui em vez de compartilhar.
WGPUBuffer MakeBuffer (WGPUDevice device, WGPUBufferUsage usage, const void *data, size_t size)
{
  WGPUBufferDescriptor desc = {};
  desc.size = size;
  desc.usage = usage | WGPUBufferUsage_CopyDst;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
  WGPUQueue queue = wgpuDeviceGetQueue(device);
  wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
  wgpuQueueRelease(queue);
  return buffer;
}

} // namespace

Disk::Disk (WGPUDevice device, int segments)
{
  // vertice central + `segments` vertices na borda do circulo unitario
  // (raio 1; o tamanho final e controlado pela Transform/Scale do Node
  // que usa este Disk, nao aqui).
  std::vector<float> coords;
  std::vector<float> texcoords;
  coords.reserve(static_cast<size_t>(segments + 2) * 2);
  texcoords.reserve(static_cast<size_t>(segments + 2) * 2);

  coords.insert(coords.end(), {0.0f, 0.0f});
  texcoords.insert(texcoords.end(), {0.5f, 0.5f});

  for (int i = 0; i <= segments; ++i) {
    float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
    float x = std::cos(angle);
    float y = std::sin(angle);
    coords.insert(coords.end(), {x, y});
    // mapeia o disco unitario [-1,1] para o espaco de textura [0,1]
    texcoords.insert(texcoords.end(), {0.5f + 0.5f * x, 0.5f - 0.5f * y});
  }

  std::vector<uint32_t> indices;
  indices.reserve(static_cast<size_t>(segments) * 3);
  for (int i = 0; i < segments; ++i) {
    indices.push_back(0);
    indices.push_back(static_cast<uint32_t>(i + 1));
    indices.push_back(static_cast<uint32_t>(i + 2));
  }
  m_nind = static_cast<uint32_t>(indices.size());

  m_coordVbo = MakeBuffer(device, WGPUBufferUsage_Vertex, coords.data(), coords.size() * sizeof(float));
  m_texcoordVbo = MakeBuffer(device, WGPUBufferUsage_Vertex, texcoords.data(), texcoords.size() * sizeof(float));
  m_ibo = MakeBuffer(device, WGPUBufferUsage_Index, indices.data(), indices.size() * sizeof(uint32_t));
}

Disk::~Disk ()
{
  wgpuBufferRelease(m_ibo);
  wgpuBufferRelease(m_texcoordVbo);
  wgpuBufferRelease(m_coordVbo);
}

DiskPtr Disk::Make (WGPUDevice device, int segments)
{
  return DiskPtr(new Disk(device, segments));
}

void Disk::Draw (StatePtr st)
{
  // registra a matriz acumulada deste no (posicao/escala) no storage
  // buffer de instancias do shader, obtendo a linha (firstInstance) que
  // o vertex shader vai indexar via @builtin(instance_index) -- mesmo
  // padrao de Quad::Draw.
  uint32_t firstInstance = static_cast<uint32_t>(st->GetShader()->CommitMatrix(st));
  WGPURenderPassEncoder pass = st->GetRenderPass();
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_coordVbo, 0, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_texcoordVbo, 0, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderSetIndexBuffer(pass, m_ibo, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderDrawIndexed(pass, m_nind, 1, 0, 0, firstInstance);
}
