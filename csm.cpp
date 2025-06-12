#include "csm.h"
#include <array>

// https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
// "Practical split scheme"
std::vector<std::pair<float, float>> CSM::split(float near, float far, uint32_t n_partitions) {
	assert(n_partitions != 0);
	std::vector<float> splits;
	for (uint32_t i = 1; i < n_partitions; ++i) {
		float r = (float)i / n_partitions;
		float log_split = near * std::pow((far / near), r);
		float uniform_split = near + (far - near) * r;
		splits.push_back(0.5f * (log_split + uniform_split));
	}

	std::vector<std::pair<float, float>> partitions(splits.size() + 1);
	for (uint32_t i = 0; i < splits.size(); ++i) {
		partitions[i].second = splits[i];
		partitions[i + 1].first = splits[i];
	}
	partitions.front().first = near;
	partitions.back().second = far;

	assert(partitions.size() == n_partitions);
	return partitions;
}

glm::vec3 CSM::ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv) {
	glm::vec4 view_space_coord = proj_inv * glm::vec4(ndc, 1.0f);
	view_space_coord = view_space_coord / view_space_coord.w;
	glm::vec4 world_space_coord = view_inv * view_space_coord;
	return world_space_coord;
}

// world space frustum
CSM::SquareBound CSM::bound_frustum(glm::mat3 light_space_inv, uint32_t resolution, const Frustum& frustum) {
	// radius and center of bounding sphere
	float r = glm::length(frustum[0] - frustum[6]) * 0.5f;
	glm::vec3 c = (frustum[0] + frustum[6]) * 0.5f;
	c = light_space_inv * c;
	glm::vec2 c_xy = glm::vec2(c);
	// expand to fit snapping
	float margin = r / (resolution - 1);
	float unit_texel = 2.0f * (r + margin) / resolution;

	// texel snapping
	glm::vec2 c_xy_snapped = glm::roundEven(c_xy / unit_texel) * unit_texel;

	SquareBound bound;
	bound.center = glm::vec3(c_xy_snapped.x, c_xy_snapped.y, c.z);
	bound.half_width = r;
	return bound;
}

std::vector<CSM::CascadeContext> CSM::csm_ortho_projections(
	PerspectiveCamera& camera,
	glm::vec3 light_dir,
	uint32_t n_cascades,
	uint32_t resolution,
	float blend_overlap) {

	//determine light space
	glm::vec3 z = glm::normalize(light_dir);
	glm::vec3 x = glm::cross(z, glm::vec3(0.0f, 1.0f, 0.0f));
	if (glm::length(x) < 10E-4) {
		// colinear
		x = glm::vec3(1.0f, 0.0f, 0.0f);
	}
	glm::vec3 y = glm::normalize(glm::cross(x, z));
	glm::mat3 light_space(x, y, z); // a left-handed coordinate system

	Frustum f_whole;
	glm::mat4 proj_inv = glm::inverse(camera.proj);
	glm::mat4 view_inv = glm::inverse(camera.view);
	glm::mat3 light_space_inv = glm::transpose(light_space);
	f_whole[0] = ndc_to_world(glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	f_whole[1] = ndc_to_world(glm::vec4(-1.0f,  1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	f_whole[2] = ndc_to_world(glm::vec4( 1.0f,  1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	f_whole[3] = ndc_to_world(glm::vec4( 1.0f, -1.0f, 0.0f, 1.0f), proj_inv, view_inv);
	f_whole[4] = ndc_to_world(glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	f_whole[5] = ndc_to_world(glm::vec4(-1.0f,  1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	f_whole[6] = ndc_to_world(glm::vec4( 1.0f,  1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	f_whole[7] = ndc_to_world(glm::vec4( 1.0f, -1.0f, 1.0f, 1.0f), proj_inv, view_inv);

	std::vector<std::pair<float, float>> partitions = split(camera.near, camera.far, n_cascades);

	// add overlap to partitions
	if (n_cascades > 1) {
		for (uint32_t i = 1; i < partitions.size(); ++i) {
			float half_overlap = blend_overlap * 0.5f;
			partitions[i - 1].second += half_overlap;
			partitions[i].first -= half_overlap;
		}
	}

	// normalize partitions
	std::vector<std::pair<float, float>> norm_partitions = partitions;
	for (auto& p : norm_partitions) {
		p.first = (p.first - camera.near) / (camera.far - camera.near);
		p.second = (p.second - camera.near) / (camera.far - camera.near);
	}

	std::vector<CascadeContext> cascade_ctxs;
	for (uint32_t i = 0; i < partitions.size(); ++i) {
		// normalized start and end
		float start_norm = (partitions[i].first - camera.near) / (camera.far - camera.near);
		float end_norm = (partitions[i].second - camera.near) / (camera.far - camera.near);

		Frustum f_part;
		f_part[0] = glm::mix(f_whole[0], f_whole[4], start_norm);
		f_part[1] = glm::mix(f_whole[1], f_whole[5], start_norm);  
		f_part[2] = glm::mix(f_whole[2], f_whole[6], start_norm);
		f_part[3] = glm::mix(f_whole[3], f_whole[7], start_norm);
		f_part[4] = glm::mix(f_whole[0], f_whole[4], end_norm);
		f_part[5] = glm::mix(f_whole[1], f_whole[5], end_norm);
		f_part[6] = glm::mix(f_whole[2], f_whole[6], end_norm);  
		f_part[7] = glm::mix(f_whole[3], f_whole[7], end_norm);

		SquareBound square_bound = bound_frustum(light_space_inv, resolution, f_part);
		float hw = square_bound.half_width;

		// TODO: Ideally near/far plane should be determined by passing the AABB of the entire scene.
		glm::mat4 ortho = glm::orthoLH_ZO(-hw, hw, -hw, hw, -6.0f * hw, 6.0f * hw);
		glm::mat4 light_view(1.0f);
		light_view = light_space_inv;
		light_view[3] = glm::vec4(-square_bound.center, 1.0f);
		
		cascade_ctxs.push_back({ partitions[i].first, partitions[i].second, light_view, ortho });
	}
	return cascade_ctxs;
}