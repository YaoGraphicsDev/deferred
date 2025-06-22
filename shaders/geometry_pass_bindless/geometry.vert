#version 460 // to support gl_BaseInstance https://www.khronos.org/opengl/wiki/Vertex_Shader/Defined_Inputs
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec4 outWorldTangent;
layout(location = 3) out flat int outMaterialId;

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
	outWorldNormal = normalize(inverse(transpose(mat3(objModelMat))) * vec3(inNormal));
	outUV = inUV;
	vec3 worldTangent = normalize(mat3(objModelMat) * vec3(inTangent));
	outWorldTangent = vec4(worldTangent, inTangent.w);
	outMaterialId = oUbos[nonuniformEXT(objId)].matId;
}