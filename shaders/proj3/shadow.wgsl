// shadow.wgsl -- passe de profundidade a partir da luz (shadow mapping).
//
// So o vertex shader existe: o pipeline do passe de sombra nao tem alvo de
// cor (Pipeline::Make com targetFormat = nullopt), apenas o depth32float.
// O "projection" do bloco global e, aqui, a matriz da LUZ (a camera usada
// no Algorithm::RenderPass e a LightCamera), em espaco "world".
//
// A normal e declarada (location 1) apenas para casar com o layout de
// vertices de Mesh/Cube (slot 0 = coord, slot 1 = normal).

struct Matrix {
  vertex: mat4x4<f32>,   // objeto -> mundo
}
@group(1) @binding(0) var<storage, read> matrix: array<Matrix>;

struct Global {
  projection: mat4x4<f32>,   // mundo -> NDC da luz
}
@group(0) @binding(0) var<uniform> global: Global;

@vertex
fn vs_main (
  @builtin(instance_index) instance_index: u32,
  @location(0) coord: vec3<f32>,
  @location(1) normal: vec3<f32>,
) -> @builtin(position) vec4<f32> {
  return global.projection * (matrix[instance_index].vertex * vec4<f32>(coord, 1.0));
}
