#pragma once

#include <GLFW/glfw3.h>
#include <functional>

class InputHandler {
public:
	InputHandler(GLFWwindow* window);

	enum class MouseButton {
		None = 0,
		Left,
		Right,
		Mid,
		MaxCount
	};

	void set_mouse_input_filter(std::function<bool(void)> filter);
	// Set MouseButton::None to handle mouse movement without dragging
	void set_mouse_drag_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_press_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_release_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_scroll_handler(std::function<void(double, double, double)> handle);

	void set_key_press_handler(std::function<void(int)> handle);

	void set_key_release_handler(std::function<void(int)> handle);

	// TODO: allow acquiring mouse position/status in key press handlers

private:
	GLFWwindow* _window = nullptr;
};