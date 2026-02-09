#pragma once

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_angle.hpp>

#include <GLFW/glfw3.h>

#include "camera.h"

//class FreeRoam {
//public:
//    enum class Movement {
//        Forward = 0,
//        Backward,
//        StrafeLeft,
//        StrafeRight,
//        Up,
//        Down,
//        Count
//    };
//
//    void begin(glm::vec3* eye, glm::vec3* center, glm::vec3* up, glm::ivec2 view_size, bool invert_y = true) {
//
//        _in_progress = true;
//
//        _view_size = view_size;
//        _center = center;
//        _radius = glm::length(eye - center);
//        _front = glm::normalize(center - eye);
//        _right = glm::normalize(glm::cross(_front, up));
//        _up = glm::normalize(glm::cross(_right, _front));
//
//        _invert_y = invert_y;
//
//        _start = map_sphere(_invert_y ? glm::ivec2(p.x, _view_size.y - p.y) : p, _view_size);
//    }
//
//    void movement_start(Movement move, float speed, glm::vec3& eye, glm::vec3& center) {
//
//    }
//
//    void movement_end(Movement move, float speed, glm::vec3& eye, glm::vec3& center) {
//
//    }
//
//    glm::vec3 _eye;
//    glm::vec3 _center;
//    glm::vec3 _up;
//};

enum class FreeRoamMovement {
    None = 0,
    Forward,
    Backward,
    StrafeLeft,
    StrafeRight,
    Up,
    Down,
    Count
};

static void update_camera_free_roam(
    GLFWwindow* window,
    glm::vec3* eye,
    glm::vec3* center,
    glm::vec3* up,
    FreeRoamMovement move,
    float speed,
    float sensitivity,
    bool invert_y = true) {
    // Get screen center
    int window_width;
    int window_height;
    glfwGetWindowSize(window, &window_width, &window_height);
    glm::vec2 screen_center = { window_width / 2.0f, window_height / 2.0f };

    // Get mouse position
    double mouse_x;
    double mouse_y;
    glfwGetCursorPos(window, &mouse_x, &mouse_y);

    // Calculate delta from center
    glm::vec2 delta = {
        mouse_x - screen_center.x,
        mouse_y - screen_center.y
    };

    // Recenter mouse cursor
    glfwSetCursorPos(window, screen_center.x, screen_center.y);

    // Calculate current forward vector from camera
    glm::vec3 current_forward = glm::normalize(*center - *eye);

    // Calculate current yaw and pitch
    float yaw, pitch;

    // Calculate pitch (vertical angle)
    constexpr float rad2deg = 180.0f / glm::pi<float>();
    constexpr float deg2rad = glm::pi<float>() / 180.0f;
    pitch = asinf(current_forward.y) * rad2deg;

    // Calculate yaw (horizontal angle) - handle all quadrants
    if (fabsf(current_forward.x) > 0.001f || fabsf(current_forward.z) > 0.001f) {
        yaw = atan2f(current_forward.z, current_forward.x) * rad2deg;
    }
    else {
        yaw = 0.0f;
    }

    // Ensure yaw is in reasonable range
    while (yaw < 0.0f) yaw += 360.0f;
    while (yaw >= 360.0f) yaw -= 360.0f;

    // Mouse look - update angles based on mouse movement
    yaw += delta.x * sensitivity;
    pitch -= delta.y * sensitivity;

    // Clamp pitch to avoid gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // Calculate new forward vector from updated angles
    glm::vec3 forward = {
        cosf(deg2rad * pitch) * cosf(deg2rad * yaw),
        sinf(deg2rad * pitch),
        cosf(deg2rad * pitch) * sinf(deg2rad * yaw)
    };
    forward = glm::normalize(forward);

    // Right and up vectors
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0, 1.0, 0.0)));
    *up = glm::cross(right, forward);

    // movements WSAD
    speed /= 30.0f;
    if (move == FreeRoamMovement::Forward) *eye += forward * speed;
    if (move == FreeRoamMovement::Backward) *eye -= forward * speed;
    if (move == FreeRoamMovement::StrafeLeft) *eye -= right * speed;
    if (move == FreeRoamMovement::StrafeRight) *eye += right * speed;
    if (move== FreeRoamMovement::Down) *eye -= *up * speed;
    if (move == FreeRoamMovement::Up) *eye += *up * speed;

    // Update camera target and up
    *center = *eye + forward;
}