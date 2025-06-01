#pragma once

#include <GLFW/glfw3.h>
#include <functional>

class InputHandler {
public:
	InputHandler(GLFWwindow* window);

	enum class MouseButton {
		Left = 0,
		Right,
		Mid,
		MaxCount
	};

	void set_mouse_input_filter(std::function<bool(void)> filter);

	void set_mouse_drag_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_press_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_release_handler(std::function<void(double, double)> handler, MouseButton button);

	void set_mouse_scroll_handler(std::function<void(double, double, double)> handle);

private:
	GLFWwindow* _window = nullptr;
};