#version 100
precision highp float;

attribute vec2 a_position;
attribute vec4 a_color;
uniform highp vec4 u_viewport;
uniform highp vec2 u_surface;
varying highp vec4 v_color;
void main()
{
    vec2 surface = a_position * u_viewport.x + u_viewport.yz;
    vec2 ndc = surface / u_surface * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = a_color;
}
