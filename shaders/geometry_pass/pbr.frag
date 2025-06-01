#version 450

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWorldTangent;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;


layout(set = 2, binding = 0) uniform MaterialUBO {
	vec4 baseColorFactor;
	vec4 mrnoFactor; // 4 factors: metallic - roughness - normal scale - occlusion strength
	uint alphaMode; // 0 - opaque, 1 - mask, 2 - blend
	float alphaCutoff;
	uint flipNormal; // 0 - dont need to flip, 1 - need to flip
} mUbo;

layout(set = 3, binding = 0) uniform sampler2D samplerBaseColor;
layout(set = 3, binding = 1) uniform sampler2D samplerNormal;
layout(set = 3, binding = 2) uniform sampler2D samplerMetallicRoughness;

void main() {
	vec4 albedo = texture(samplerBaseColor, inUV);
	if (mUbo.alphaMode == 1 && albedo.w < mUbo.alphaCutoff) {
		discard;
	}
	outAlbedo = albedo * mUbo.baseColorFactor;

	float metallicFactor = mUbo.mrnoFactor.x;
	float roughnessFactor = mUbo.mrnoFactor.y;
	float normalScale = mUbo.mrnoFactor.z;
	
	vec3 n = normalize(inWorldNormal);
	vec3 t = normalize(inWorldTangent.xyz);
	vec3 b = cross(n, t) * inWorldTangent.w;
	mat3 tbn = mat3(t, b, n);
	vec3 tbnCoord = texture(samplerNormal, inUV).xyz * 2.0 - 1.0;
	tbnCoord.xy *= vec2(normalScale);
	tbnCoord = normalize(tbnCoord);
	vec3 worldNormal = tbn * tbnCoord;
	if (mUbo.flipNormal == 1 && !gl_FrontFacing) {
		worldNormal = -worldNormal;
	}
	outNormal = vec4(worldNormal * 0.5 + 0.5, 1.0f);

	outMetallicRoughness = texture(samplerMetallicRoughness, inUV) * vec4(metallicFactor, roughnessFactor, 0.0f, 0.0f);
}