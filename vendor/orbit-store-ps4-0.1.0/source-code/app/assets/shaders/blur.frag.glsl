#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;

uniform sampler2D u_texture;
uniform vec2 u_step;
varying highp vec2 v_uv;
void main()
{
    vec4 c = texture2D(u_texture, v_uv) * 0.227027;
    c += (texture2D(u_texture, v_uv + u_step * 1.384615) +
          texture2D(u_texture, v_uv - u_step * 1.384615)) * 0.316216;
    c += (texture2D(u_texture, v_uv + u_step * 3.230769) +
          texture2D(u_texture, v_uv - u_step * 3.230769)) * 0.070270;
    gl_FragColor = vec4(c.rgb, 1.0);
}
