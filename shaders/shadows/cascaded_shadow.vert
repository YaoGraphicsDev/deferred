#version 460 // to support gl_BaseInstance https://www.khronos.org/opengl/wiki/Vertex_Shader/Defined_Inputs
#extension GL_EXT_nonuniform_qualifier : require

#define MAX_CASCADES 4

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectView;
} fUbo;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    int matId;
} oUbos[];

void main() {
	uint objId = gl_BaseInstance;
	mat4 objModelMat = oUbos[nonuniformEXT(objId)].model;
	gl_Position = fUbo.projectView * objModelMat * vec4(inPosition, 1.0f);
}