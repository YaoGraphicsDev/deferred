#pragma once
#include "glm/glm.hpp"
#include <array>

struct FrustumUtils {
	typedef std::array<glm::vec3, 8> Frustum;

	static glm::vec3 ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv);

	// in world space
	static Frustum view_frustum_vertices(glm::mat4 proj_inv, glm::mat4 view_inv);

	static glm::vec4 plane(glm::vec3 a, glm::vec3 b, glm::vec3 c);
};
