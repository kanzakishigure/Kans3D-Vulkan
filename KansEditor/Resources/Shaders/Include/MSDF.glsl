#ifndef KANS_MSDF_GLSL
#define KANS_MSDF_GLSL
// RGBA MTSDF: RGB median for sharp corners; alpha for true distance effects.
float median(vec3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }
float msdfScreenPixelRange(vec2 uv, vec2 atlasSize, float pixelRange) {
    vec2 unitRange = vec2(pixelRange) / atlasSize;
    vec2 screenTexSize = 1.0 / max(fwidth(uv), vec2(1e-8));
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}
float msdfCoverage(vec4 distanceSample, float screenPixelRange) {
    return clamp((median(distanceSample.rgb) - 0.5) * screenPixelRange + 0.5, 0.0, 1.0);
}
// Width and softness are in screen pixels. Includes the fill; subtract coverage for a ring.
float msdfOutline(vec4 distanceSample, float screenPixelRange, float width) {
    return clamp((distanceSample.a - 0.5) * screenPixelRange + max(width, 0.0) + 0.5, 0.0, 1.0);
}
// Pass the alpha sample at (uv - shadowOffsetUV) to position the shadow.
float msdfSoftShadow(float alphaSDF, float screenPixelRange, float spread, float softness) {
    float distancePx = (alphaSDF - 0.5) * screenPixelRange + spread;
    return smoothstep(-max(softness, 0.5), max(softness, 0.5), distancePx);
}
#endif
