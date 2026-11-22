#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;

varying highp vec4 v_color;
void main()
{
    gl_FragColor = v_color;
}
