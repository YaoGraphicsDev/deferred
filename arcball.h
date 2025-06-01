#pragma once

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_angle.hpp>

class Arcball {
public:
	void begin(glm::vec3 eye, glm::vec3 center, glm::vec3 up, glm::ivec2 p, glm::ivec2 view_size, bool invert_y = true) {

		_in_progress = true;

		_view_size = view_size;
		_center = center;
		_radius = glm::length(eye - center);
		_front = glm::normalize(center - eye);
		_right = glm::normalize(glm::cross(_front, up));
		_up = glm::normalize(glm::cross(_right, _front));

		_invert_y = invert_y;

		_start = map_sphere(_invert_y ? glm::ivec2(p.x, _view_size.y - p.y) : p, _view_size);
	}

	void progress(glm::ivec2 p, glm::vec3& eye, glm::vec3& up) {
		if (!_in_progress) {
			return;
		}

		glm::vec3 current = map_sphere(_invert_y ? glm::ivec2(p.x, _view_size.y - p.y) : p, _view_size);
		if (glm::length(glm::cross(current, _start)) < 0.001) {
			// tiny movement. Ignore
			return;
		}
		glm::quat rot = glm::rotation(current, _start);

		glm::mat3 mat(_right, _up, -_front);
		
		glm::vec3 front = mat * glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
		up = mat * glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));

		eye = _center - front * _radius;
	}

	void end() {
		_in_progress = false;
	}

private:
	glm::vec3 map_sphere(glm::ivec2 p, glm::ivec2 view_size) {
		// map to [-1, 1]
		float radius = glm::length(glm::vec2(view_size));
		glm::vec2 sp = (glm::vec2(p) * 2.0f - glm::vec2(view_size)) / glm::vec2(radius);
		float len2 = glm::length2(sp);

		if (len2 <= 1.0f) {
			// on the sphere
			return glm::vec3(sp.x, sp.y, sqrt(1.0f - len2));
		}
		else {
			// outside the sphere, project to sphere surface
			return glm::normalize(glm::vec3(sp.x, sp.y, 0.0f));
		}
	}

	glm::ivec2 _view_size = glm::ivec2(0);

	glm::vec3 _center = glm::vec3(0.0f);
	float _radius = 0.0f;

	glm::vec3 _right = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 _front = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 _up = glm::vec3(0.0f, 0.0f, 1.0f);
	
	glm::vec3 _start = glm::vec3(1.0f, 0.0f, 0.0f);

	bool _in_progress = false;
	bool _invert_y = true;
};