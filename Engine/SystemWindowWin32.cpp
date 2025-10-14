#include "SystemWindow.h"
#include "Basic/BasicMemory.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

static RECT ComputeWindowInnerRect(HWND hwnd, RECT outer_rect) {
	u32 dpi = GetDpiForWindow(hwnd);
	s32 border_size  = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
	s32 frame_size_x = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + border_size;
	s32 frame_size_y = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + border_size;
	
	RECT inner_rect;
	inner_rect.left   = outer_rect.left   + frame_size_x;
	inner_rect.right  = outer_rect.right  - frame_size_x;
	inner_rect.top    = outer_rect.top    + frame_size_y;
	inner_rect.bottom = outer_rect.bottom - frame_size_y;
	
	return inner_rect;
}

static LRESULT WINAPI WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) {
		return true;
	}
	
	auto* window = (SystemWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	
	switch (message) {
	case WM_CLOSE: {
		window->should_close = true;
		return 0;
	} case WM_SIZE: {
		window->size.x = (u32)LOWORD(lparam);
		window->size.y = (u32)HIWORD(lparam);
		return 0;
	} case WM_NCHITTEST: {
		LRESULT result = 0;
		
		// When the window is not maximized, add a border around the window that can be used for resizing.
		if (IsZoomed(hwnd) == false) {
			RECT outer_rect;
			GetClientRect(hwnd, &outer_rect);
			
			RECT inner_rect = ComputeWindowInnerRect(hwnd, outer_rect);
			
			POINT cursor_position;
			cursor_position.x = (s16)LOWORD(lparam);
			cursor_position.y = (s16)HIWORD(lparam);
			ScreenToClient(hwnd, &cursor_position);
			
			bool left   = (outer_rect.left   < cursor_position.x) && (cursor_position.x < inner_rect.left);
			bool right  = (inner_rect.right  < cursor_position.x) && (cursor_position.x < outer_rect.right);
			bool top    = (outer_rect.top    < cursor_position.y) && (cursor_position.y < inner_rect.top);
			bool bottom = (inner_rect.bottom < cursor_position.y) && (cursor_position.y < outer_rect.bottom);
			
			if (top && left) {
				result = HTTOPLEFT;
			} else if (top && right) {
				result = HTTOPRIGHT;
			} else if (bottom && left) {
				result = HTBOTTOMLEFT;
			} else if (bottom && right) {
				result = HTBOTTOMRIGHT;
			} else if (left) {
				result = HTLEFT;
			} else if (right) {
				result = HTRIGHT;
			} else if (top) {
				result = HTTOP;
			} else if (bottom) {
				result = HTBOTTOM;
			}
		}
		
		// If the border around the window is not hovered, check if titlebar is hovered.
		// Otherwise we're inside the window and ImGui should take the inputs.
		if (result == 0) {
			result = window->titlebar_hovered ? HTCAPTION : HTCLIENT;
		}
		
		return result;
	} case WM_NCCALCSIZE: {
		if (IsZoomed(hwnd)) {
			auto& rect = *(RECT*)lparam;
			rect = ComputeWindowInnerRect(hwnd, rect);
		}
		return 0;
	} case WM_SYSCOMMAND: {
		// Disable ALT application menu.
		if ((wparam & 0xFFF0) == SC_KEYMENU) {
			return 0;
		}
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}
	}
	
	return DefWindowProcW(hwnd, message, wparam, lparam);
}
compile_const auto* default_window_class_name = L"DefaultWindow";

SystemWindow* SystemCreateWindow(StackAllocator* alloc, String window_name) {
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
	
	auto window_name_utf16 = StringUtf8ToUtf16(alloc, window_name);
	
	auto hwnd = CreateWindowExW(
		0,
		window_desc.lpszClassName,
		(wchar_t*)window_name_utf16.data,
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
	
	window->hwnd = hwnd;
	window->size = 0;
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
	if (window->requested_state == WindowState::Maximized && window->state != WindowState::Maximized) {
		ShowWindow((HWND)window->hwnd, SW_MAXIMIZE);
	} else if (window->requested_state == WindowState::Minimized && window->state != WindowState::Minimized) {
		ShowWindow((HWND)window->hwnd, SW_MINIMIZE);
	} else if (window->requested_state == WindowState::Floating && window->state != WindowState::Floating) {
		ShowWindow((HWND)window->hwnd, SW_RESTORE);
	}
	window->requested_state = WindowState::None;
	
	MSG message = {};
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	
	if (IsIconic((HWND)window->hwnd)) {
		window->state = WindowState::Minimized;
	} else if (IsZoomed((HWND)window->hwnd)) {
		window->state = WindowState::Maximized;
	} else {
		window->state = WindowState::Floating;
	}
}
