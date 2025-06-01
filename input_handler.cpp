#include "input_handler.h"
#include <iostream>
#include <array>
#include <algorithm>

struct ButtonState {
    InputHandler::MouseButton button = InputHandler::MouseButton::MaxCount;
    bool is_holding = false;
};
ButtonState button_state;

std::array<std::function<void(double, double)>, 3> drag_handlers;
std::array<std::function<void(double, double)>, 3> press_handlers;
std::array<std::function<void(double, double)>, 3> release_handlers;
std::function<void(double, double, double)> scroll_handler;
std::function<bool(void)> mouse_filter = nullptr;

void InputHandler::set_mouse_input_filter(std::function<bool(void)> filter) {
    mouse_filter = filter;
}

void InputHandler::set_mouse_drag_handler(std::function<void(double, double)> handler, MouseButton button) {
    drag_handlers[(size_t)button] = handler;
}

void InputHandler::set_mouse_press_handler(std::function<void(double, double)> handler, MouseButton button) {
    press_handlers[(size_t)button] = handler;
}

void InputHandler::set_mouse_release_handler(std::function<void(double, double)> handler, MouseButton button) {
    release_handlers[(size_t)button] = handler;
}

void InputHandler::set_mouse_scroll_handler(std::function<void(double, double, double)> handle) {
    scroll_handler = handle;
}

void mouse_click_callback(GLFWwindow* window, int button, int action, int mods) {
    if (mouse_filter && !mouse_filter()) {
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        button_state.button = InputHandler::MouseButton::Left;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        button_state.button = InputHandler::MouseButton::Right;
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        button_state.button = InputHandler::MouseButton::Mid;
    }
    else {}

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    if (action == GLFW_PRESS) {
        if (press_handlers[(size_t)button_state.button]) {
            press_handlers[(size_t)button_state.button](x, y);
        }
        button_state.is_holding = true;
    }
    else {
        if (release_handlers[(size_t)button_state.button]) {
            release_handlers[(size_t)button_state.button](x, y);
        }
        button_state.is_holding = false;
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mouse_filter && !mouse_filter()) {
        return;
    }

    if (button_state.is_holding && drag_handlers[(size_t)button_state.button]) {
        drag_handlers[(size_t)button_state.button](xpos, ypos);
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (mouse_filter && !mouse_filter()) {
        return;
    }

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    if (scroll_handler) {
        scroll_handler(x, y, yoffset);
    }
}

InputHandler::InputHandler(GLFWwindow* window) {
	_window = window;

    glfwSetMouseButtonCallback(_window, mouse_click_callback);
    glfwSetCursorPosCallback(_window, cursor_position_callback);
    glfwSetScrollCallback(_window, scroll_callback);
}