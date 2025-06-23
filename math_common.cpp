#include "math_common.h"

glm::vec3 FrustumUtils::ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv) {
	glm::vec4 view_space_coord = proj_inv * glm::vec4(ndc, 1.0f);
	view_space_coord = view_space_coord / view_space_coord.w;
	glm::vec4 world_space_coord = view_inv * view_space_coord;
	return world_space_coord;
}

// in world space
FrustumUtils::Frustum FrustumUtils::view_frustum_vertices(glm::mat4 proj_inv, glm::mat4 view_inv) {
	Frustum f;
	/*near top left*/		f[0] = ndc_to_world(glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	/*near bottom left*/	f[1] = ndc_to_world(glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	/*near bottom right*/	f[2] = ndc_to_world(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	/*near top right*/		f[3] = ndc_to_world(glm::vec4(1.0f, -1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	/*far top left*/		f[4] = ndc_to_world(glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	/*far bottom left*/		f[5] = ndc_to_world(glm::vec4(-1.0f, 1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	/*far bottom right*/	f[6] = ndc_to_world(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	/*far top right*/		f[7] = ndc_to_world(glm::vec4(1.0f, -1.0f, 1.0f, 1.0f), proj_inv, view_inv);

	return f;
}

glm::vec4 FrustumUtils::plane(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
	glm::vec3 cross = glm::cross(b - a, c - a);
	float len = glm::length(cross);
	if (len < 10E-5) {
		return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	glm::vec3 n = cross / len;
	float d = -glm::dot(n, a);
	return glm::vec4(n, d);
}
