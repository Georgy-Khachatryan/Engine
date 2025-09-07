#pragma once
#include "Basic/Basic.h"

struct SystemWindow {
	void* hwnd = nullptr;
	bool should_close = false;
};

SystemWindow* SystemCreateWindow(struct StackAllocator* alloc, const wchar_t* window_name);
void SystemReleaseWindow(SystemWindow* window);
void SystemPollWindowEvents(SystemWindow* window);

