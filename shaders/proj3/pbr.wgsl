// pbr.wgsl -- Projeto 3 (INF1761)
//
// Iluminacao por fragmento em espaco "world", com dois modelos de
// sombreamento alternaveis em tempo de execucao (params.x):
//   0 = Phong classico (referencia, como nas tarefas anteriores)
//   1 = PBR microfacet: D = GGX, G = Smith (Schlick-GGX), F = Schlick
// Duas fontes:
//   - luz PONTUAL no alto da cena (a do bloco "light_*"), que projeta a
//     sombra (shadow mapping + PCF, com um unico mapa em perspectiva
//     olhando para baixo);
//   - luz de SPOT na lampada da luminaria (bulb_*), sem sombra, apontada
//     para onde a cupula abre, com atenuacao 1/d^2.
// Saida: tone mapping ACES + gamma (se a superficie nao for sRGB).

struct Matrix {
  vertex: mat4x4<f32>,   // objeto -> mundo
  normal: mat4x4<f32>,   // transposta da inversa, p/ normais
}
@group(1) @binding(0) var<storage, read> matrix: array<Matrix>;

struct Global {
  projection: mat4x4<f32>,        // mundo -> NDC da camera
  // luz pontual do alto (preenchida por PointLight, em espaco "world")
  light_ambient: vec3<f32>,
  light_diffuse: vec3<f32>,
  light_specular: vec3<f32>,
  light_position: vec4<f32>,
  camera_position: vec4<f32>,
  // preenchidos pela aplicacao via Shader::SetValue
  light_view_proj: mat4x4<f32>,   // mundo -> clip da luz (shadow map)
  bulb_position: vec4<f32>,       // spot da lampada, em mundo
  bulb_direction: vec4<f32>,      // direcao do spot (unitaria), em mundo
  bulb_color: vec4<f32>,          // rgb = cor, a = intensidade
  bulb_cone: vec4<f32>,           // x = cos do angulo externo, y = cos do interno
  params: vec4<f32>,              // x modo (0 Phong, 1 PBR), y sombra on/off, z raio PCF, w bias constante
  params2: vec4<f32>,             // x gamma manual (1 = aplicar), y bias de normal (unid. de mundo), z 1/tamanho do mapa, w passo do PCF (em texels)
  extra: vec4<f32>,               // x = distancia de referencia^2 da luz do alto (abaixo dela, radiancia = light_diffuse)
}
@group(0) @binding(0) var<uniform> global: Global;

struct Material {
  base_color: vec3<f32>,
  opacity: f32,
  specular_color: vec3<f32>,     // so usado no modo Phong
  shininess: f32,                // so usado no modo Phong
  metallic: f32,                 // so PBR
  roughness: f32,                // so PBR
  emissive: f32,                 // luz propria (lampada)
  checker: f32,                  // > 0.5: xadrez procedural (chao)
}
@group(2) @binding(0) var<uniform> material: Material;

@group(3) @binding(0) var shadow_map: texture_depth_2d;
@group(3) @binding(1) var shadow_sampler: sampler_comparison;

const PI: f32 = 3.14159265358979;

struct VertexOutput {
  @builtin(position) clip_position: vec4<f32>,
  @location(0) world: vec3<f32>,
  @location(1) normal: vec3<f32>,
}

@vertex
fn vs_main (
  @builtin(instance_index) instance_index: u32,
  @location(0) coord: vec3<f32>,
  @location(1) normal: vec3<f32>,
) -> VertexOutput {
  let m = matrix[instance_index];
  let w = m.vertex * vec4<f32>(coord, 1.0);
  var out: VertexOutput;
  out.world = w.xyz;
  out.normal = (m.normal * vec4<f32>(normal, 0.0)).xyz;
  out.clip_position = global.projection * w;
  return out;
}

// ---------------------------------------------------------------- sombra
// Fracao de luz que chega ao ponto (1 = iluminado, 0 = na sombra), media
// de (2r+1)^2 comparacoes de profundidade (PCF). r = 0 -> sombra dura.
fn shadow_visibility (world: vec3<f32>, n: vec3<f32>) -> f32 {
  if (global.params.y < 0.5) { return 1.0; }
  // bias de normal: desloca o ponto ao longo da normal antes de projetar,
  // o que combate o "shadow acne" sem afastar a sombra dos objetos
  let p = global.light_view_proj * vec4<f32>(world + n * global.params2.y, 1.0);
  let ndc = p.xyz / p.w;
  let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  let depth = ndc.z - global.params.w;
  let r = i32(global.params.z);
  let texel = global.params2.z * global.params2.w;   // passo entre amostras do PCF
  var sum = 0.0;
  var count = 0.0;
  for (var j = -r; j <= r; j++) {
    for (var i = -r; i <= r; i++) {
      let o = vec2<f32>(f32(i), f32(j)) * texel;
      sum += textureSampleCompareLevel(shadow_map, shadow_sampler, uv + o, depth);
      count += 1.0;
    }
  }
  var vis = sum / count;
  // fora do frustum da luz: sem informacao de sombra
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0) { vis = 1.0; }
  return vis;
}

// ------------------------------------------------------------------- PBR
fn d_ggx (n_h: f32, alpha: f32) -> f32 {
  let a2 = alpha * alpha;
  let d = n_h * n_h * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
fn g_schlick (n_x: f32, k: f32) -> f32 {
  return n_x / (n_x * (1.0 - k) + k);
}
fn g_smith (n_v: f32, n_l: f32, rough: f32) -> f32 {
  let k = (rough + 1.0) * (rough + 1.0) / 8.0;   // k de iluminacao direta
  return g_schlick(n_v, k) * g_schlick(n_l, k);
}
fn f_schlick (v_h: f32, f0: vec3<f32>) -> vec3<f32> {
  return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - v_h, 5.0);
}

// Contribuicao de UMA luz (direcao l, radiancia incidente `radiance`).
fn shade_light (l: vec3<f32>, n: vec3<f32>, v: vec3<f32>, radiance: vec3<f32>,
                albedo: vec3<f32>, metallic: f32, rough: f32) -> vec3<f32> {
  let n_l = dot(n, l);
  if (n_l <= 0.0) { return vec3<f32>(0.0); }
  if (global.params.x < 0.5) {
    // Phong classico (nao e conservador de energia; so para comparacao)
    let refl = reflect(-l, n);
    let spec = material.specular_color * pow(max(dot(refl, v), 0.0), material.shininess);
    return (albedo * n_l + spec) * radiance / PI;
  }
  let h = normalize(l + v);
  let n_v = max(dot(n, v), 1e-4);
  let n_h = max(dot(n, h), 0.0);
  let v_h = max(dot(v, h), 0.0);
  let f0 = mix(vec3<f32>(0.04), albedo, metallic);
  let alpha = rough * rough;
  let d = d_ggx(n_h, alpha);
  let g = g_smith(n_v, n_l, rough);
  let f = f_schlick(v_h, f0);
  let spec = d * g * f / (4.0 * n_v * n_l + 1e-4);
  let kd = (vec3<f32>(1.0) - f) * (1.0 - metallic);
  return (kd * albedo / PI + spec) * radiance * n_l;
}

fn aces (x: vec3<f32>) -> vec3<f32> {
  let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fs_main (in: VertexOutput) -> @location(0) vec4<f32> {
  let v = normalize(global.camera_position.xyz - in.world);
  var n = normalize(in.normal);
  // dupla face: malhas abertas (cupula) vistas por dentro
  if (dot(n, v) < 0.0) { n = -n; }

  var albedo = material.base_color;
  if (material.checker > 0.5) {
    let t = i32(floor(in.world.x / 40.0)) + i32(floor(in.world.z / 40.0));
    if ((t & 1) == 0) { albedo = albedo * 0.62; }
  }
  let metallic = clamp(material.metallic, 0.0, 1.0);
  let rough = clamp(material.roughness, 0.04, 1.0);
  let f0 = mix(vec3<f32>(0.04), albedo, metallic);

  // ambiente (aproximacao simples: sem IBL)
  var color = global.light_ambient * (albedo * (1.0 - metallic) + f0 * metallic * 0.5);

  // reflexo de um ambiente procedural (ceu azulado em cima, chao escuro
  // embaixo): sem ele um metal liso so teria o brilho das luzes. Apenas PBR.
  if (global.params.x > 0.5) {
    let refl_dir = reflect(-v, n);
    let sky = mix(vec3<f32>(0.10, 0.10, 0.12), vec3<f32>(0.55, 0.68, 0.95), clamp(refl_dir.y * 0.5 + 0.5, 0.0, 1.0));
    let n_v0 = max(dot(n, v), 1e-4);
    // dieletricos quase nao mostram o ambiente (senao a cena fica com ar de
    // nevoa azulada, principalmente o chao em angulo rasante)
    let env_k = mix(0.06, 1.0, metallic) * (1.0 - 0.7 * rough);
    color += sky * f_schlick(n_v0, f0) * env_k;
  }

  // luz pontual do alto da cena (com sombra); 1/d^2, normalizada para que
  // o ponto logo abaixo dela receba exatamente light_diffuse
  let top_vec = global.light_position.xyz - in.world;
  let top_d2 = max(dot(top_vec, top_vec), 1.0);
  let top_radiance = global.light_diffuse * (global.extra.x / top_d2);
  let top_l = top_vec * inverseSqrt(top_d2);
  if (dot(n, top_l) > 0.0) {
    let vis = shadow_visibility(in.world, n);
    color += vis * shade_light(top_l, n, v, top_radiance, albedo, metallic, rough);
  }

  // spot da lampada da luminaria (atenuacao 1/d^2, cone suave, sem sombra)
  let to_bulb = global.bulb_position.xyz - in.world;
  let b_d2 = max(dot(to_bulb, to_bulb), 1.0);
  let b_l = to_bulb * inverseSqrt(b_d2);
  let b_c = dot(-b_l, normalize(global.bulb_direction.xyz));
  let b_cone = smoothstep(global.bulb_cone.x, global.bulb_cone.y, b_c);
  let bulb_radiance = global.bulb_color.rgb * global.bulb_color.a * b_cone / b_d2;
  color += shade_light(b_l, n, v, bulb_radiance, albedo, metallic, rough);

  // luz propria da lampada
  color += material.base_color * material.emissive;

  color = aces(color);
  if (global.params2.x > 0.5) { color = pow(color, vec3<f32>(1.0 / 2.2)); }
  return vec4<f32>(color, 1.0);
}