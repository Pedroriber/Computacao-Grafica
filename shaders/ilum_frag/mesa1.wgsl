// Grupos: global (0), matrizes (1), material (2), texturas (3).
// Geometria e iluminação são calculadas no espaço do mundo.

struct Matrix {
  vertex: mat4x4<f32>,
  normal: mat4x4<f32>,
}
@group(1) @binding(0) var<storage, read> matrix: array<Matrix>;

// Usar vec4 para os campos adicionais mantém o alinhamento dos uniforms.
struct Global {
  projection: mat4x4<f32>,
  light_ambient: vec3<f32>,
  light_diffuse: vec3<f32>,
  light_specular: vec3<f32>,
  light_position: vec4<f32>,
  light_direction: vec3<f32>,
  light_spot_cos_cutoff: f32,
  light_spot_exponent: f32,
  camera_position: vec4<f32>,
  light2_position: vec4<f32>,
  light2_color: vec4<f32>,
  fog_color: vec4<f32>,
  fog_params: vec4<f32>,
  clip_plane: vec4<f32>,
  effects: vec4<f32>,
  proj_position: vec4<f32>,
  proj_right: vec4<f32>,
  proj_up: vec4<f32>,
  proj_forward: vec4<f32>,
  proj_params: vec4<f32>,
}
@group(0) @binding(0) var<uniform> global: Global;

struct Material {
  base_color: vec3<f32>,
  opacity: f32,

  specular_color: vec3<f32>,
  shininess: f32,

  roughness: vec4<f32>,
}
@group(2) @binding(0) var<uniform> material: Material;

@group(3) @binding(0) var decal_texture: texture_2d<f32>;
@group(3) @binding(1) var decal_sampler: sampler;
@group(3) @binding(2) var slide_texture: texture_2d<f32>;

struct VertexOutput {
  @builtin(position) clip_position: vec4<f32>,
  @location(0) normal: vec3<f32>,
  @location(1) position: vec3<f32>,
  @location(2) texcoord: vec2<f32>,
}

@vertex
fn vs_main (
  @builtin(instance_index) instance_index: u32,
  @location(0) coord: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(3) texcoord: vec2<f32>,
) -> VertexOutput {
  let m = matrix[instance_index];
  var out: VertexOutput;
  let veye = (m.vertex * vec4<f32>(coord, 1.0)).xyz;
  out.normal = (m.normal * vec4<f32>(normal, 0.0)).xyz;
  out.position = veye;
  out.texcoord = texcoord;
  out.clip_position = global.projection * (m.vertex * vec4<f32>(coord, 1.0));
  return out;
}

@fragment
fn fs_main (in: VertexOutput) -> @location(0) vec4<f32> {
  // --- amostragens no topo: textureSample exige fluxo uniforme ---
  let decal = textureSample(decal_texture, decal_sampler, in.texcoord);

  // Projeta o fragmento no plano da imagem do projetor.
  let rel = in.position - global.proj_position.xyz;
  let pz = dot(rel, global.proj_forward.xyz);
  let px = dot(rel, global.proj_right.xyz);
  let py = dot(rel, global.proj_up.xyz);
  let inv_z = 1.0 / max(pz, 0.0001);
  let slide_uv = vec2<f32>(0.5 + 0.5 * px * inv_z * global.proj_params.x,
                           0.5 - 0.5 * py * inv_z * global.proj_params.x);
  let slide = textureSample(slide_texture, decal_sampler, clamp(slide_uv, vec2<f32>(0.0), vec2<f32>(1.0)));
  var slide_inside = 0.0;
  if (pz > 0.0 && all(slide_uv >= vec2<f32>(0.0)) && all(slide_uv <= vec2<f32>(1.0))) {
    slide_inside = 1.0;
  }

  if (global.effects.y > 0.5 && dot(global.clip_plane.xyz, in.position) + global.clip_plane.w < 0.0) {
    discard;
  }

  let normal = normalize(in.normal);
  let view = normalize(global.camera_position.xyz - in.position);

  var shin = material.shininess;
  if (material.roughness.x > 0.0) {
    let r = clamp(material.roughness.x, 0.05, 1.0);
    shin = max(2.0 / (r * r) - 2.0, 1.0);
  }

  let spot_dir = normalize(global.light_position.xyz - in.position);
  let proj_dir = normalize(global.proj_position.xyz - in.position);
  var light = spot_dir;
  var light_scale = vec3<f32>(0.0);
  if (global.effects.w > 0.5) {
    light = proj_dir;
    light_scale = slide.rgb * slide.a * slide_inside;
  } else {
    let c = dot(-spot_dir, normalize(global.light_direction));
    var k_spot = 0.0;
    if (c >= global.light_spot_cos_cutoff) {
      if (global.light_spot_exponent == 0.0) {
        k_spot = 1.0;
      } else {
        k_spot = pow(max(c, 0.0), global.light_spot_exponent);
      }
    }
    light_scale = vec3<f32>(k_spot);
  }

  var color = material.base_color * global.light_ambient;
  let ndotl = dot(normal, light);
  if (ndotl > 0.0) {
    color += material.base_color * global.light_diffuse * light_scale * ndotl;
    let refl = normalize(reflect(-light, normal));
    color += material.specular_color * global.light_specular * light_scale *
             pow(max(0.0, dot(refl, view)), shin);
  }

  if (global.effects.z > 0.5) {
    let l2 = normalize(global.light2_position.xyz - in.position);
    let n2 = dot(normal, l2);
    if (n2 > 0.0) {
      color += material.base_color * global.light2_color.rgb * n2;
      let r2 = normalize(reflect(-l2, normal));
      color += material.specular_color * global.light2_color.rgb *
               pow(max(0.0, dot(r2, view)), shin);
    }
  }

  var result = vec4<f32>(color, 1.0) * decal;

  if (global.effects.x > 0.5) {
    let d = length(global.camera_position.xyz - in.position);
    let f = clamp((global.fog_params.y - d) / (global.fog_params.y - global.fog_params.x), 0.0, 1.0);
    result = vec4<f32>(mix(global.fog_color.rgb, result.rgb, f), 1.0);
  }
  return result;
}
