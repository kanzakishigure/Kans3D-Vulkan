#version 450
#include "Parameters.glsl"
layout(location=0) out vec4 Color;
void main() { Color = p.Tint; }
