#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicMath.h"

struct SystemWindow {
	void* hwnd = nullptr;
	uint2 size = 0;
	bool should_close = false;
};

SystemWindow* SystemCreateWindow(StackAllocator* alloc, const wchar_t* window_name);
void SystemReleaseWindow(SystemWindow* window);
void SystemPollWindowEvents(SystemWindow* window);

