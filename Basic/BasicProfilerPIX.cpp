#include "Basic.h"

#if defined(USE_PIX)
#define WIN32_LEAN_AND_MEAN
#include <SDK/D3D12/include/d3d12.h>
#include <SDK/WinPixEventRuntime/include/pix3.h>

void ProfilerBeginScope(const char* label) { PIXBeginEvent(PIX_COLOR_DEFAULT, label); }
void ProfilerEndScope() { PIXEndEvent(); }

void ProfilerBeginScope(const char* label, ID3D12GraphicsCommandList* command_list) { PIXBeginEvent(command_list, PIX_COLOR_DEFAULT, label); }
void ProfilerEndScope(ID3D12GraphicsCommandList* command_list) { PIXEndEvent(command_list); }

void ProfilerBeginScope(const char* label, ID3D12CommandQueue* command_list) { PIXBeginEvent(command_list, PIX_COLOR_DEFAULT, label); }
void ProfilerEndScope(ID3D12CommandQueue* command_list) { PIXEndEvent(command_list); }

#endif // defined(USE_PIX)
