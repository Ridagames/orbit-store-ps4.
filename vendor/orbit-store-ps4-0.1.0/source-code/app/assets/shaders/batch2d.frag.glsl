#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;

uniform highp vec4 u_viewport;
uniform sampler2D u_texture;
uniform sampler2D u_font0;
uniform sampler2D u_font1;
uniform sampler2D u_font2;
uniform sampler2D u_font3;
uniform sampler2D u_font4;
uniform sampler2D u_font5;
uniform sampler2D u_font6;
varying highp vec4 v_pack0;
varying highp vec4 v_pack1;
varying highp vec4 v_top;
varying highp vec4 v_bottom;
varying highp vec4 v_border;
varying highp vec4 v_params;
varying highp vec4 v_extra;

float round_box(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float triangle(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

float star5(vec2 p, float r, float rf)
{
    const vec2 k1 = vec2(0.809016994375, -0.587785252292);
    const vec2 k2 = vec2(-k1.x, k1.y);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

void main()
{
    vec2 v_local = v_pack0.xy;
    vec2 v_half = v_pack0.zw;
    vec2 v_virtual = v_pack1.xy;
    vec2 v_uv = v_pack1.zw;
    // Gradient axis of the pre-centred local coordinate, recomputed from the
    // packed varyings to stay inside the ES2 varying highp budget.
    float v_t = (v_params.w < 0.5 && v_extra.y > 0.5)
                    ? clamp((v_local.x + v_half.x) / max(2.0 * v_half.x, 1e-3), 0.0, 1.0)
                    : clamp((v_local.y + v_half.y) / max(2.0 * v_half.y, 1e-3), 0.0, 1.0);
    float px = 1.0 / u_viewport.x; // virtual units per output pixel
    int shape = int(v_params.w + 0.5);
    vec4 fill = mix(v_top, v_bottom, v_t);
    vec4 color;
    if (shape == 0)
    {
        float radius = min(v_params.x, min(v_half.x, v_half.y));
        float d = round_box(v_local, v_half, radius);
        color = fill;
        if (v_params.y > 0.0)
        {
            float inner = clamp(0.5 - (d + v_params.y) / px, 0.0, 1.0);
            color = mix(v_border, fill, inner);
        }
        color.a *= clamp(0.5 - d / px, 0.0, 1.0);
    }
    else if (shape == 1)
    {
        // The font atlases stay bound to their own units; the slot picks one.
        int slot = int(v_params.z + 0.5);
        float sdf;
        if (slot == 1)
            sdf = texture2D(u_font0, v_uv).r;
        else if (slot == 2)
            sdf = texture2D(u_font1, v_uv).r;
        else if (slot == 3)
            sdf = texture2D(u_font2, v_uv).r;
        else if (slot == 4)
            sdf = texture2D(u_font3, v_uv).r;
        else if (slot == 5)
            sdf = texture2D(u_font4, v_uv).r;
        else if (slot == 6)
            sdf = texture2D(u_font5, v_uv).r;
        else if (slot == 7)
            sdf = texture2D(u_font6, v_uv).r;
        else
            sdf = texture2D(u_texture, v_uv).r;
        float dist = (sdf - 0.5) * 2.0 * v_params.x;
        color = vec4(fill.rgb, fill.a * clamp(dist / px + 0.5, 0.0, 1.0));
    }
    else if (shape == 2)
    {
        float radius = min(v_params.x, min(v_half.x, v_half.y));
        float d = round_box(v_local, v_half, radius);
        color = vec4(fill.rgb, fill.a * (1.0 - smoothstep(-v_params.z, v_params.z, d)));
    }
    else if (shape == 3)
    {
        color = texture2D(u_texture, v_uv) * fill;
        if (v_params.x > 0.0)
        {
            float radius = min(v_params.x, min(v_half.x, v_half.y));
            color.a *= clamp(0.5 - round_box(v_local, v_half, radius) / px, 0.0, 1.0);
        }
    }
    else if (shape == 4)
    {
        vec2 pa = v_virtual - v_extra.xy;
        vec2 ba = v_extra.zw - v_extra.xy;
        float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
        float d = length(pa - ba * h) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else if (shape == 8)
    {
        // Box with 45 degree corner cuts: the box distance, limited by the
        // diagonal plane through each corner.
        float cut = min(v_params.x, min(v_half.x, v_half.y));
        vec2 q = abs(v_local) - v_half;
        float box = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        float d = max(box, (q.x + q.y + cut) * 0.70710678);
        color = fill;
        if (v_params.y > 0.0)
        {
            float inner = clamp(0.5 - (d + v_params.y) / px, 0.0, 1.0);
            color = mix(v_border, fill, inner);
        }
        color.a *= clamp(0.5 - d / px, 0.0, 1.0);
    }
    else if (shape == 7)
    {
        // Ring sector, after Inigo Quilez's arc: turn the sector upright,
        // fold it on its axis, then measure to the ring or to its end.
        float rb = 0.5 * v_params.y;
        float ra = min(v_half.x, v_half.y) - rb;
        float half_sweep = 0.5 * v_extra.x;
        float mid = v_params.x + half_sweep;
        float cm = cos(mid);
        float sm = sin(mid);
        vec2 p = vec2(v_local.x, -v_local.y);
        p = vec2(abs(p.x * cm - p.y * sm), p.x * sm + p.y * cm);
        vec2 sc = vec2(sin(half_sweep), cos(half_sweep));
        float ring = abs(length(p) - ra);
        float d;
        if (v_extra.y > 0.5)
            d = max(ring - rb, p.x * sc.y - p.y * sc.x);
        else
            d = ((sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : ring) - rb;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else if (shape == 6)
    {
        vec2 p = vec2(v_local.x, -v_local.y);
        float d = star5(p, min(v_half.x, v_half.y), 0.45);
        if (v_params.y > 0.0)
            d = abs(d + 0.5 * v_params.y) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else
    {
        float d = triangle(v_local, vec2(0.0, -v_half.y), vec2(-v_half.x, v_half.y),
                           vec2(v_half.x, v_half.y));
        if (v_params.y > 0.0)
            d = abs(d + 0.5 * v_params.y) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    if (color.a <= 0.0)
        discard;
    gl_FragColor = color;
}
