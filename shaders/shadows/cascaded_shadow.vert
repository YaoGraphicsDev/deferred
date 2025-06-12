#version 450

#define MAX_CASCADES 4

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectView;
} fUbo;

layout(set = 1, binding = 0) uniform ObjectUBO {
	mat4 model;
} oUbo;

void main() {
	gl_Position = fUbo.projectView * oUbo.model * vec4(inPosition, 1.0f);
}