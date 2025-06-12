#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLDR;

layout(set = 0, binding = 0) uniform sampler2D samplerHDR;

void main() {
    vec3 hdr = texture(samplerHDR, inUV).rgb;
    hdr = max(hdr, vec3(0.0f));
    // reinhard tone mapping
    outLDR = vec4(hdr / (hdr + vec3(1.0)), 1.0f);
}