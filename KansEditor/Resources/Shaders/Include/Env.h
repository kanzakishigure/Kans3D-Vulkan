#pragma once

layout(set = 1, binding = 0) uniform samplerCube U_IrradianceMap;
layout(set = 1, binding = 1) uniform samplerCube U_PrefilterMap;


layout(set = 3, binding = 0) uniform sampler2D U_BrdfLUTMap;

