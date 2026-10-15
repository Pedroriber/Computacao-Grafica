// spot.wgsl (colocar em shaders/ilum_frag/)
//
// Variante de ilum_frag/lit.wgsl para uma luz de spot (classe SpotLight).
// Continua sendo iluminacao por fragmento: vs_main so transforma posicao e
// normal e entrega os dois interpolados; tudo o resto (vetor da luz, vetor
// de vista, coeficiente de spot e formula de Phong) e calculado em fs_main,
// por pixel.
//
// Diferencas em relacao ao lit.wgsl:
//  - Global ganha light_direction, light_spot_cos_cutoff e
//    light_spot_exponent (campos escritos por SpotLight::Map/Set).
//  - O vetor da luz nao pode mais ser interpolado a partir dos vertices:
//    k_spot depende do ponto iluminado e muda dentro de cada triangulo
//    (a borda do cone cruza as faces). Por isso interpolamos a POSICAO do
//    ponto e calculamos l em cada fragmento.
//  - O vetor de vista usa camera_position (posicao da camera no espaco de
//    iluminacao), em vez de -veye: -veye so e o vetor de vista quando o
//    espaco de iluminacao e "camera" (camera na origem). Como a cena usa
//    espaco "world", -veye apontaria para a origem do mundo, e nao para a
//    camera, e o brilho especular sairia no lugar errado.

struct Matrix {
  vertex: mat4x4<f32>,
  normal: mat4x4<f32>,
}
@group(1) @binding(0) var<storage, read> matrix: array<Matrix>;

struct Global {
  projection: mat4x4<f32>,   // espaco de iluminacao -> NDC
  light_ambient: vec3<f32>,
  light_diffuse: vec3<f32>,
  light_specular: vec3<f32>,
  light_position: vec4<f32>,         // s_pos (w = 1), no espaco de iluminacao
  light_direction: vec3<f32>,        // s_dir, unitaria, no espaco de iluminacao
  light_spot_cos_cutoff: f32,        // cos(theta), theta = angulo de abertura
  light_spot_exponent: f32,          // s_exp
  camera_position: vec4<f32>,        // camera no espaco de iluminacao
}
@group(0) @binding(0) var<uniform> global: Global;

struct Material {
  base_color: vec3<f32>,
  opacity: f32,

  specular_color: vec3<f32>,
  shininess: f32,
}
@group(2) @binding(0) var<uniform> material: Material;

struct VertexOutput {
  @builtin(position) clip_position: vec4<f32>,
  @location(0) normal: vec3<f32>,
  @location(1) position: vec3<f32>,   // ponto p, no espaco de iluminacao
}

@vertex
fn vs_main (@builtin(instance_index) instance_index: u32, @location(0) coord: vec3<f32>, @location(1) normal: vec3<f32>) -> VertexOutput {
  let m = matrix[instance_index];
  var out: VertexOutput;
  let veye = (m.vertex * vec4<f32>(coord, 1.0)).xyz;
  out.normal = (m.normal * vec4<f32>(normal, 0.0)).xyz;
  out.position = veye;
  out.clip_position = global.projection * (m.vertex * vec4<f32>(coord, 1.0));
  return out;
}

@fragment
fn fs_main (in: VertexOutput) -> @location(0) vec4<f32> {
  // renormaliza: a interpolacao entre vertices desnormaliza a normal
  let normal = normalize(in.normal);

  // l = (s_pos - p) normalizado: aponta do ponto iluminado para a fonte
  let light = normalize(global.light_position.xyz - in.position);
  let view = normalize(global.camera_position.xyz - in.position);

  // c = -l . s_dir : cosseno do angulo entre a direcao do spot e o ponto
  let c = dot(-light, normalize(global.light_direction));

  // k_spot = max(c,0)^s_exp se c >= cos(theta), 0 caso contrario.
  // Para s_exp = 0 a intensidade e constante no cone (k_spot = 1); o caso
  // e tratado a parte porque pow(0, 0) nao e bem definido em WGSL.
  var k_spot = 0.0;
  if (c >= global.light_spot_cos_cutoff) {
    if (global.light_spot_exponent == 0.0) {
      k_spot = 1.0;
    } else {
      k_spot = pow(max(c, 0.0), global.light_spot_exponent);
    }
  }

  // ambiente nao e multiplicado por k_spot: fora do cone a cena nao fica
  // totalmente preta, so escura (o termo ambiente e o que sobra la).
  // Para o spot apagar tambem o ambiente, multiplicar a linha abaixo por k_spot.
  var color = material.base_color * global.light_ambient;

  // difusa e especular: a cor da luz e multiplicada por k_spot
  let ndotl = dot(normal, light);
  if (ndotl > 0.0 && k_spot > 0.0) {
    color += material.base_color * global.light_diffuse * k_spot * ndotl;
    let refl = normalize(reflect(-light, normal));
    color += material.specular_color * global.light_specular * k_spot *
             pow(max(0.0, dot(refl, view)), material.shininess);
  }
  return vec4<f32>(color, 1.0);
}
