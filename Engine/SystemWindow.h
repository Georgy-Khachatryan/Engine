#pragma once
#include "Basic/Basic.h"

struct SystemWindow {
	void* hwnd = nullptr;
	u32 width  = 0;
	u32 height = 0;
	bool should_close = false;
};

SystemWindow* SystemCreateWindow(StackAllocator* alloc, const wchar_t* window_name);
void SystemReleaseWindow(SystemWindow* window);
void SystemPollWindowEvents(SystemWindow* window);

