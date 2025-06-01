#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec4 outWorldTangent;


layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectView;
} fUbo;

layout(set = 1, binding = 0) uniform ObjectUBO {
	mat4 model;
} oUbo;

void main() {
	gl_Position = fUbo.projectView * oUbo.model * vec4(inPosition, 1.0f);
	outWorldNormal = normalize(inverse(transpose(mat3(oUbo.model))) * vec3(inNormal));
	outUV = inUV;
	vec3 worldTangent = normalize(mat3(oUbo.model) * vec3(inTangent));
	outWorldTangent = vec4(worldTangent, inTangent.w);
}
