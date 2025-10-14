#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"
#include "Basic/BasicString.h"

enum struct WindowState : u32 {
	None      = 0,
	Maximized = 1,
	Minimized = 2,
	Floating  = 3,
};

struct SystemWindow {
	void* hwnd = nullptr;
	uint2 size = 0;
	WindowState state = WindowState::Maximized;
	WindowState requested_state = WindowState::None;
	bool titlebar_hovered = false;
	bool should_close = false;
};

SystemWindow* SystemCreateWindow(StackAllocator* alloc, String window_name);
void SystemReleaseWindow(SystemWindow* window);
void SystemPollWindowEvents(SystemWindow* window);

