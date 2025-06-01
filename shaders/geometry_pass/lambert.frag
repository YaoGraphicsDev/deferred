#version 450

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;

layout(set = 2, binding = 0) uniform MaterialUBO {
	vec4 baseColorFactor;
	uint alphaMode; // 0 - opaque, 1 - mask, 2 - blend
	float alphaCutoff;
	uint flipNormal; // 0 - dont need to flip, 1 - need to flip
} mUbo;

layout(set = 3, binding = 0) uniform sampler2D samplerBaseColor;

void main() {
	vec4 albedo = texture(samplerBaseColor, inUV);
	if (mUbo.alphaMode == 1 && albedo.w < mUbo.alphaCutoff) {
		discard;
	}
	outAlbedo = texture(samplerBaseColor, inUV) * mUbo.baseColorFactor;
	if (mUbo.flipNormal == 1 && !gl_FrontFacing) {
		// TODO: normal computation may not be correct
		outNormal = vec4(-inWorldNormal, 1.0f);
	} else {
		outNormal = vec4(inWorldNormal, 1.0f);
	}
	outMetallicRoughness = vec4(0.0f);
}