#version 100
precision highp float;

attribute vec2 a_corner;
attribute vec4 a_rect;
attribute vec4 a_top;
attribute vec4 a_bottom;
attribute vec4 a_border;
attribute vec4 a_params;
attribute vec4 a_extra;
uniform highp vec4 u_viewport; // scale, offset x, offset y
uniform highp vec2 u_surface;
varying highp vec4 v_pack0; // local centred xy, half size
varying highp vec4 v_pack1; // virtual xy, uv
varying highp vec4 v_top;
varying highp vec4 v_bottom;
varying highp vec4 v_border;
varying highp vec4 v_params;
varying highp vec4 v_extra;
void main()
{
    vec2 corner = a_corner;
    float shape = a_params.w;
    vec2 size = a_rect.zw;
    // Grow shapes by their softness plus one output pixel for anti-aliasing;
    // glyph and image quads map texels exactly and are not grown.
    float pad = ((shape > 0.5 && shape < 1.5) || (shape > 2.5 && shape < 3.5))
                    ? 0.0
                    : a_params.z + 1.0 / u_viewport.x;
    vec2 local = mix(vec2(-pad), size + vec2(pad), corner);
    vec2 centred = local - 0.5 * size;
    vec2 virt = a_rect.xy + local;
    // Rounded rectangles, shadows and triangles may turn about their centre: the quad
    // rotates, the distance field keeps working in the unrotated frame.
    bool box = shape < 0.5 || (shape > 1.5 && shape < 2.5) || (shape > 4.5 && shape < 5.5);
    if (box && a_extra.x != 0.0)
    {
        float c = cos(a_extra.x);
        float s = sin(a_extra.x);
        virt = a_rect.xy + 0.5 * size + vec2(c * centred.x - s * centred.y,
                                             s * centred.x + c * centred.y);
    }
    v_pack0 = vec4(centred, 0.5 * size);
    v_pack1 = vec4(virt, mix(a_extra.xy, a_extra.zw, corner));
    v_top = a_top;
    v_bottom = a_bottom;
    v_border = a_border;
    v_params = a_params;
    v_extra = a_extra;
    vec2 surface = virt * u_viewport.x + u_viewport.yz;
    vec2 ndc = surface / u_surface * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
