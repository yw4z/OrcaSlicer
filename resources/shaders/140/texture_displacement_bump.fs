#version 140

// Fast, geometry-free preview of texture displacement: perturbs the *shading* normal from the
// height texture's local gradient (a bump map), faded out by the per-vertex paint weight. The
// true, exact result is what "Bake" produces via libslic3r/TextureDisplacement.cpp on the CPU.
//
// The bake displaces each surface point along its normal by H = +/- depth_mm * (h(uv) - midlevel),
// with uv from the layer's projection. The perturbed normal is the analytic
//
//     N' = normalize(N - (dH/da) * T - (dH/db) * B)
//
// over any orthonormal surface tangent pair (T, B), where the two slopes are real mm-per-mm
// derivatives. Two things have to be right for the preview's apparent depth to match the bake's:
// the tangent frame the gradient is expressed in, and the uv->mm scale that turns a texel
// difference into a slope. Getting the scale wrong is a uniform flattening (a raw texel difference
// is dh over one texel step, not over one mm); getting the frame wrong tilts the bump along the
// wrong axes.
//
// Two projection paths:
//   * Triplanar (use_vertex_uv = 0): uv and the tangent axes are derived in-shader from the dominant
//     normal axis, mirroring libslic3r's project_planar()/apply_uv_transform(), and the slope is
//     formed analytically (there is a closed-form uv, so 1 uv unit is exactly tiling_scale mm).
//   * Precomputed uv (use_vertex_uv = 1, used for LSCM): uv comes per-vertex from the CPU (the LSCM
//     unwrap with island placement + tiling/rotation/offset already folded in), and the perturbed
//     normal is built with Mikkelsen's method -- the surface gradient taken straight from the
//     screen-space derivatives of the sampled height and position. This makes no uv->mm scale
//     assumption, which matters because an LSCM map is conformal, not isometric: the local mm-per-uv
//     varies across the chart, so a single global 1/tiling factor (what an earlier version used) got
//     the apparent depth wrong. This path is also what makes the fast preview follow the UV editor:
//     move an island and its uv -- hence its bump -- moves with it.

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.3

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

uniform vec4 uniform_color;
uniform bool volume_mirrored;

uniform mat4 view_model_matrix;
uniform mat3 view_normal_matrix;

uniform sampler2D height_tex;
uniform vec2      height_tex_texel; // (1/width, 1/height) of height_tex
uniform float      depth_mm;
uniform float      tiling_scale;
uniform float      rotation_rad;
uniform vec2       uv_offset;
uniform bool       invert;
uniform bool       use_vertex_uv; // true: sample at vertex_uv with a derived tangent frame (LSCM)
// A 2x3 affine (columns packed as lin = (m00, m01, m10, m11), tr = (m02, m12)) applied to the uv of
// the island currently being dragged in the UV editor (island_active > 0.5). Identity when nothing is
// dragged, so this whole path is a no-op then. Lets a UV island drag move the bump on the model with
// only a uniform update 
uniform vec4       island_delta_lin;
uniform vec2       island_delta_tr;

in vec3  clipping_planes_dots;
in vec4  model_pos;
in vec4  world_pos;
in float weight;
in float island_active;
in vec2  vertex_uv;

out vec4 out_color;

// The two model-space axes the triplanar planar coordinate is read off, per dominant normal
// component - same choice libslic3r's project_planar() makes, so planar.x runs along t, planar.y
// along b.
void projection_axes(vec3 n, out vec3 t, out vec3 b)
{
    vec3 an = abs(n);
    if (an.x >= an.y && an.x >= an.z) {        // planar = p.yz
        t = vec3(0.0, 1.0, 0.0);
        b = vec3(0.0, 0.0, 1.0);
    } else if (an.y >= an.x && an.y >= an.z) { // planar = p.xz
        t = vec3(1.0, 0.0, 0.0);
        b = vec3(0.0, 0.0, 1.0);
    } else {                                   // planar = p.xy
        t = vec3(1.0, 0.0, 0.0);
        b = vec3(0.0, 1.0, 0.0);
    }
}

vec2 project_uv(vec3 p, vec3 n)
{
    vec3 an = abs(n);
    vec2 planar = (an.x >= an.y && an.x >= an.z) ? p.yz : ((an.y >= an.x && an.y >= an.z) ? p.xz : p.xy);
    planar *= (tiling_scale > 1e-6) ? (1.0 / tiling_scale) : 1.0;
    float cs = cos(rotation_rad);
    float sn = sin(rotation_rad);
    return vec2(planar.x * cs - planar.y * sn, planar.x * sn + planar.y * cs) + uv_offset;
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));
    if (volume_mirrored)
        triangle_normal = -triangle_normal;

    if (use_vertex_uv) {
        // Precomputed-uv (LSCM) path - Mikkelsen's surface-gradient bump ("Bump Mapping
        // Unparametrized Surfaces on the GPU"). The perturbed normal is derived straight from the
        // screen-space derivatives of the *sampled height* and the position, so it is scale-exact
        // with no uv->mm assumption at all - which is the whole point here: an LSCM map is conformal,
        // not isometric, so the local mm-per-uv varies across the chart and the earlier "one global
        // 1/tiling factor" got the depth visibly wrong. dFdx(h) captures the true on-screen rate of
        // change however the chart is stretched or however fine the tiling is.
        //
        // use_vertex_uv is a uniform, so this whole branch is uniform control flow and the texture
        // derivatives are well defined; the paint weight gates the result by a plain multiply (k)
        // rather than a per-fragment branch, keeping it that way.
        // The dragged island's uv rides a uniform affine so its bump moves without a rebuild; every
        // other vertex (island_active == 0) samples its baked uv unchanged.
        vec2 uv = (island_active > 0.5)
                    ? vec2(dot(island_delta_lin.xy, vertex_uv), dot(island_delta_lin.zw, vertex_uv)) + island_delta_tr
                    : vertex_uv;
        float h = texture(height_tex, uv).r;
        float k = (invert ? -1.0 : 1.0) * depth_mm * clamp(weight, 0.0, 1.0);
        vec3  sigmaS = dFdx(model_pos.xyz);
        vec3  sigmaT = dFdy(model_pos.xyz);
        vec3  R1 = cross(sigmaT, triangle_normal);
        vec3  R2 = cross(triangle_normal, sigmaS);
        float det = dot(sigmaS, R1);
        float dHdx = k * dFdx(h);
        float dHdy = k * dFdy(h);
        if (abs(det) > 1e-12)
            triangle_normal = normalize(triangle_normal - (dHdx * R1 + dHdy * R2) / det);
    } else if (weight > 0.0) {
        // Triplanar path: uv and the tangent axes are reconstructed in-shader from the dominant
        // normal component (see header). The gradient is expressed analytically because there is a
        // closed-form uv here, unlike the LSCM case.
        vec2 uv = project_uv(model_pos.xyz, triangle_normal);
        vec3 t, b;
        projection_axes(triangle_normal, t, b);

        float hL = texture(height_tex, uv - vec2(height_tex_texel.x, 0.0)).r;
        float hR = texture(height_tex, uv + vec2(height_tex_texel.x, 0.0)).r;
        float hD = texture(height_tex, uv - vec2(0.0, height_tex_texel.y)).r;
        float hU = texture(height_tex, uv + vec2(0.0, height_tex_texel.y)).r;

        // Central difference, per uv unit (not per texel).
        vec2 dh_duv = vec2((hR - hL) / (2.0 * height_tex_texel.x), (hU - hD) / (2.0 * height_tex_texel.y));

        // uv -> mm is 1/tiling_scale for the triplanar projection, so this turns the uv-space
        // gradient into a real surface slope.
        float inv_tiling = (tiling_scale > 1e-6) ? (1.0 / tiling_scale) : 1.0;
        float amplitude  = (invert ? -1.0 : 1.0) * depth_mm * inv_tiling * clamp(weight, 0.0, 1.0);
        // uv was rotated by project_uv() while t/b are the unrotated model axes, so rotate the
        // gradient back into the axes' frame.
        float cs = cos(rotation_rad);
        float sn = sin(rotation_rad);
        vec2  slope = amplitude * vec2(dh_duv.x * cs + dh_duv.y * sn, -dh_duv.x * sn + dh_duv.y * cs);

        vec3 gradient = slope.x * t + slope.y * b;
        gradient -= triangle_normal * dot(triangle_normal, gradient);
        triangle_normal = normalize(triangle_normal - gradient);
    }

    vec3 eye_normal = normalize(view_normal_matrix * triangle_normal);
    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    vec2 intensity = vec2(0.0);
    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    vec3 position = (view_model_matrix * model_pos).xyz;
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}
