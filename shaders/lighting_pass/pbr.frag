#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLit;

struct Light {
    float intensity;
    vec3 color;
    vec3 direction;
};

layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectInv;
    mat4 viewInv;
    Light lights[16];
} fUbo;

layout(set = 0, binding = 1) uniform sampler2D samplerDepth;
layout(set = 0, binding = 2) uniform sampler2D samplerAlbedo;
layout(set = 0, binding = 3) uniform sampler2D samplerNormal;
layout(set = 0, binding = 4) uniform sampler2D samplerMetallicRoughness;

void main() {
    // Just show position for the moment
    float depth = texture(samplerDepth, inUV).r;
    vec4 ndc = vec4(inUV * 2.0f - 1.0f, depth, 1.0f);
    vec4 viewSpaceCoord = fUbo.projectInv * ndc;
    viewSpaceCoord = viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
    vec4 worldSpaceCoord = fUbo.viewInv * viewSpaceCoord;

    outLit = worldSpaceCoord;
}