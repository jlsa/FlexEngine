﻿#pragma once

#include "Typedefs.h"
#include "GameContext.h"
#include "InputManager.h"

#include <string>

struct GLFWwindow;

class Window
{
public:
	enum class CursorMode
	{
		NORMAL,
		HIDDEN,
		DISABLED
	};

	Window(const std::string& title, glm::vec2 size, GameContext& gameContext);
	virtual ~Window();

	// Return time in miliseconds since app start
	virtual float GetTime() = 0;

	virtual void Update(const GameContext& gameContext);
	virtual void PollEvents() = 0;

	glm::vec2i GetSize() const;
	virtual void SetSize(int width, int height) = 0;
	bool HasFocus() const;

	void SetTitleString(const std::string& title);
	void SetShowFPSInTitleBar(bool showFPS);
	void SetShowMSInTitleBar(bool showMS);
	// Set to 0 to update window title every frame
	void SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds);

	virtual void SetCursorMode(CursorMode mode) = 0;

	GLFWwindow* IsGLFWWindow();

protected:
	// Callbacks
	virtual void KeyCallback(InputManager::KeyCode keycode, InputManager::Action action, int mods);
	virtual void MouseButtonCallback(InputManager::MouseButton mouseButton, InputManager::Action action, int mods);
	virtual void WindowFocusCallback(int focused);
	virtual void CursorPosCallback(double x, double y);
	virtual void WindowSizeCallback(int width, int height);

#if COMPILE_OPEN_GL || COMPILE_VULKAN
	// GL Windows
	friend void GLFWKeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);
	friend void GLFWMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods);
	friend void GLFWWindowFocusCallback(GLFWwindow* glfwWindow, int focused);
	friend void GLFWCursorPosCallback(GLFWwindow* glfwWindow, double x, double y);
	friend void GLFWWindowSizeCallback(GLFWwindow* glfwWindow, int width, int height);
#endif // COMPILE_OPEN_GL || COMPILE_VULKAN

	// D3D Windows
	//friend LRESULT D3DKeyCallback(HWND hWnd, int keyCode, WPARAM wParam, LPARAM lParam);
	//friend LRESULT D3DMouseButtonCallback(HWND hWnd, int button, WPARAM wParam, LPARAM lParam);
	//friend LRESULT D3DWindowFocusCallback(HWND hWnd, int focused);
	//friend LRESULT D3DCursorPosCallback(HWND hWnd, double x, double y);
	//friend LRESULT D3DWindowSizeCallback(HWND hWnd, int width, int height);

	//void UpdateWindowSize(int width, int height);
	//void UpdateWindowSize(glm::vec2i windowSize);
	//void UpdateWindowFocused(int focused);

	virtual void SetWindowTitle(const std::string& title) = 0;

	// Store this privately so we can access it in callbacks
	// Should be updated with every call to Update()
	GameContext& m_GameContextRef;

	std::string m_TitleString;
	glm::vec2i m_Size;
	bool m_HasFocus;

	bool m_ShowFPSInWindowTitle;
	bool m_ShowMSInWindowTitle;

	float m_UpdateWindowTitleFrequency;
	float m_SecondsSinceTitleUpdate;

private:
	std::string GenerateWindowTitle(float dt);

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

};
