#pragma once

#include "glm/glm.hpp"
#include "camera.h"
#include <array>
#include <vector>

class CSM {
public:
	struct CascadeContext {
		float z_begin;
		float z_end;
		glm::mat4 light_view;
		glm::mat4 light_proj;
	};
	static std::vector<CascadeContext> csm_ortho_projections(
		PerspectiveCamera& camera,
		glm::vec3 light_dir,
		uint32_t n_cascades,
		uint32_t resolution,
		float blend_overlap);

private: 
	static std::vector<std::pair<float, float>> split(float near, float far, uint32_t n_partitions);

	static glm::vec3 ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv);

	typedef std::array<glm::vec3, 8> Frustum;

	struct SquareBound {
		glm::vec3 center;
		float half_width;
	};

	static SquareBound bound_frustum(glm::mat3 light_space_inv, uint32_t resolution, const Frustum& frustum);
};
