#include "SystemWindow.h"
#include "Basic/BasicMemory.h"

#include <Windows.h>


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

static LRESULT WINAPI WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) {
		return true;
	}
	
	auto* window = (SystemWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	
	LRESULT result = 0;
	
	switch (message) {
	case WM_CLOSE: {
		window->should_close = true;
		break;
	} case WM_SIZE: {
		window->width  = (u32)LOWORD(lparam);
		window->height = (u32)HIWORD(lparam);
		break;
	} default: {
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}
	}
	
	return result;
}
compile_const auto* default_window_class_name = L"DefaultWindow";

SystemWindow* SystemCreateWindow(StackAllocator* alloc, const wchar_t* window_name) {
	WNDCLASSEXW window_desc = {};
	window_desc.cbSize        = sizeof(WNDCLASSEXW);
	window_desc.style         = 0;
	window_desc.lpfnWndProc   = &WindowProc;
	window_desc.cbClsExtra    = 0;
	window_desc.cbWndExtra    = 0;
	window_desc.hInstance     = GetModuleHandleW(nullptr);
	window_desc.hIcon         = nullptr;
	window_desc.hCursor       = nullptr;
	window_desc.hbrBackground = nullptr;
	window_desc.lpszMenuName  = nullptr;
	window_desc.lpszClassName = default_window_class_name;
	window_desc.hIconSm       = nullptr;
	RegisterClassExW(&window_desc);
	
	auto hwnd = CreateWindowExW(
		0,
		window_desc.lpszClassName,
		window_name,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		nullptr,
		nullptr,
		window_desc.hInstance,
		nullptr
	);
	DebugAssert(hwnd != nullptr, "Failed to create a window.");
	
	auto* window = NewFromAlloc(alloc, SystemWindow);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
	
	window->hwnd   = hwnd;
	window->width  = 0;
	window->height = 0;
	window->should_close = false;
	
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	
	return window;
}

void SystemReleaseWindow(SystemWindow* window) {
	bool success = DestroyWindow((HWND)window->hwnd) != 0;
	DebugAssert(success, "Failed to destroy window.");

	success = UnregisterClassW(default_window_class_name, GetModuleHandleW(nullptr)) != 0;
	DebugAssert(success, "Failed to unregister window class.");
}

void SystemPollWindowEvents(SystemWindow* window) {
	MSG message = {};
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}
