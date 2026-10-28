// Grupos: global (0), matrizes (1), material (2), texturas (3).
// Locations: coord 0, normal 1, tangent 2, texcoord 3.

const FOG_COLOR: vec3<f32> = vec3<f32>(0.70, 0.75, 0.80);
const FOG_START: f32 = 5.0;
const FOG_END: f32 = 8.0;

const SLIDE_POS: vec3<f32> = vec3<f32>(0.9, 3.0, 0.55);
const SLIDE_TARGET: vec3<f32> = vec3<f32>(0.9, 1.0, 0.55);
const SLIDE_UP: vec3<f32> = vec3<f32>(0.0, 0.0, -1.0);
const SLIDE_TAN_HALF: f32 = 0.14;
const SLIDE_ASPECT: f32 = 1.0;
const SLIDE_INTENSITY: f32 = 1.0;
// Mistura o decal com branco para evitar que a textura escureça o slide.
const SLIDE_DECAL_BLEND: f32 = 0.5;
const NORMAL_MAP_STRENGTH: f32 = 4.0;

struct Matrix {
  vertex: mat4x4<f32>,
  normal: mat4x4<f32>,
}
@group(1) @binding(0) var<storage, read> matrix: array<Matrix>;

struct Global {
  projection: mat4x4<f32>,
  light_ambient: vec3<f32>,
  light_diffuse: vec3<f32>,
  light_specular: vec3<f32>,
  light_direction: vec3<f32>,
  clip_height: f32,
  camera_position: vec4<f32>,
}
@group(0) @binding(0) var<uniform> global: Global;

struct Material {
  base_color: vec3<f32>,
  opacity: f32,

  specular_color: vec3<f32>,
  shininess: f32,
}
@group(2) @binding(0) var<uniform> material: Material;

@group(3) @binding(0) var decal_texture: texture_2d<f32>;
@group(3) @binding(1) var decal_sampler: sampler;
@group(3) @binding(2) var normal_texture: texture_2d<f32>;
@group(3) @binding(3) var slide_texture: texture_2d<f32>;

struct VertexOutput {
  @builtin(position) clip_position: vec4<f32>,
  @location(0) normal: vec3<f32>,
  @location(1) position: vec3<f32>,
  @location(2) texcoord: vec2<f32>,
  @location(3) tangent: vec3<f32>,
}

@vertex
fn vs_main (
  @builtin(instance_index) instance_index: u32,
  @location(0) coord: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) tangent: vec3<f32>,
  @location(3) texcoord: vec2<f32>,
) -> VertexOutput {
  let m = matrix[instance_index];
  var out: VertexOutput;
  let veye = (m.vertex * vec4<f32>(coord, 1.0)).xyz;
  out.normal = (m.normal * vec4<f32>(normal, 0.0)).xyz;
  out.tangent = (m.vertex * vec4<f32>(tangent, 0.0)).xyz;
  out.position = veye;
  out.texcoord = texcoord;
  out.clip_position = global.projection * (m.vertex * vec4<f32>(coord, 1.0));
  return out;
}

fn slide_light (p: vec3<f32>) -> vec3<f32> {
  let f = normalize(SLIDE_TARGET - SLIDE_POS);
  let r = normalize(cross(f, SLIDE_UP));
  let u = cross(r, f);
  let d = p - SLIDE_POS;
  let depth = dot(d, f);
  let s = max(depth, 0.0001) * SLIDE_TAN_HALF;
  let x = dot(d, r) / s;
  let y = dot(d, u) / (s * SLIDE_ASPECT);
  let uv = vec2<f32>(0.5 + 0.5 * x, 0.5 - 0.5 * y);
  let c = textureSampleLevel(slide_texture, decal_sampler, uv, 0.0);
  let inside = depth > 0.0 && abs(x) <= 1.0 && abs(y) <= 1.0;
  return select(vec3<f32>(0.0), c.rgb * c.a * SLIDE_INTENSITY, inside);
}

@fragment
fn fs_main (in: VertexOutput) -> @location(0) vec4<f32> {
  let decal = textureSample(decal_texture, decal_sampler, in.texcoord);
  let nmap = textureSample(normal_texture, decal_sampler, in.texcoord).xyz * 2.0 - 1.0;
  let slide = slide_light(in.position);

  if (in.position.y > global.clip_height) {
    discard;
  }

  var normal = normalize(in.normal);
  if (textureDimensions(normal_texture).x > 1u) {
    let t = normalize(in.tangent - normal * dot(normal, in.tangent));
    let b = cross(normal, t);
    normal = normalize(t * nmap.x * NORMAL_MAP_STRENGTH +
                       b * nmap.y * NORMAL_MAP_STRENGTH +
                       normal * nmap.z);
  }

  let view = normalize(global.camera_position.xyz - in.position);
  var color = material.base_color * global.light_ambient;

  let l1 = -normalize(global.light_direction);
  let nl1 = dot(normal, l1);
  if (nl1 > 0.0) {
    color += material.base_color * global.light_diffuse * nl1;
    let r1 = reflect(-l1, normal);
    color += material.specular_color * global.light_specular *
             pow(max(0.0, dot(r1, view)), material.shininess);
  }

  let l2 = normalize(SLIDE_POS - in.position);
  let nl2 = dot(normal, l2);
  var projected = vec3<f32>(0.0);
  if (nl2 > 0.0) {
    projected += material.base_color * slide * nl2;
    let r2 = reflect(-l2, normal);
    projected += material.specular_color * slide *
                 pow(max(0.0, dot(r2, view)), material.shininess);
  }

  let slide_albedo = mix(decal.rgb, vec3<f32>(1.0), SLIDE_DECAL_BLEND);
  var rgb = color * decal.rgb + projected * slide_albedo;

  let dist = length(global.camera_position.xyz - in.position);
  let f = clamp((FOG_END - dist) / (FOG_END - FOG_START), 0.0, 1.0);
  rgb = mix(FOG_COLOR, rgb, f);

  return vec4<f32>(rgb, 1.0);
}
