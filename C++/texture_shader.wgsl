// texture_shader.wgsl
//
// Variante de shaders/2d/shader.wgsl com um grupo de textura (texture_2d +
// sampler) no lugar do grupo "material" de cor solida. Os grupos 0 (matriz
// de instancia) e 2 (projecao) sao identicos ao shader.wgsl original -- so
// o grupo 1 muda de proposito (era ColorBlock, agora e textura).
//
// location 3 para uv (em vez de 0/1/2) para casar com a convencao
// Shape::LOC::TEXCOORD definida em shape.h.

struct Matrix {
  vertex: mat4x4<f32>,       // objeto -> espaco global (2D nao ilumina)
}
@group(0) @binding(0) var<storage, read> matrix: array<Matrix>;

struct Global {
  projection: mat4x4<f32>,   // espaco de iluminacao -> NDC
}
@group(2) @binding(0) var<uniform> global: Global;

@group(1) @binding(0) var tex: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main (
  @builtin(instance_index) instance_index: u32,
  @location(0) pos: vec2<f32>,
  @location(3) uv: vec2<f32>,
) -> VertexOutput {
  var out: VertexOutput;
  out.position = global.projection * (matrix[instance_index].vertex * vec4<f32>(pos, 0.0, 1.0));
  out.uv = uv;
  return out;
}

@fragment
fn fs_main (in: VertexOutput) -> @location(0) vec4<f32> {
  return textureSample(tex, samp, in.uv);
}
