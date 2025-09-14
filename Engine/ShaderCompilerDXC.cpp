#include "ShaderCompiler.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApi.h"

#include <Windows.h>
#include <SDK/dxc/inc/dxcapi.h>
#include <stdio.h>


struct ShaderCompiler {
	IDxcCompiler3* dxc_compiler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	
	IDxcIncludeHandler* default_include_handler = nullptr;
};

template<typename ResourceT>
static void SafeReleaseDXC(ResourceT*& resource) {
	if (resource) resource->Release();
	resource = nullptr;
}

compile_const wchar_t* target_profiles[(u32)ShaderType::Count] = {
	L"cs_6_6",
	L"vs_6_6",
	L"ps_6_6",
};

compile_const wchar_t* entry_point_names[(u32)ShaderType::Count] = {
	L"MainCS",
	L"MainVS",
	L"MainPS",
};

compile_const wchar_t* shader_type_defines[(u32)ShaderType::Count] = {
	L"COMPUTE_SHADER",
	L"VERTEX_SHADER",
	L"PIXEL_SHADER",
};

ArrayView<u8> CompileShader(ShaderCompiler* compiler, StackAllocator* alloc, String shader_path, ShaderType shader_type) {
	IDxcBlob* bytecode_blob = nullptr;
	defer{ SafeReleaseDXC(bytecode_blob); };
	
	{
		TempAllocationScope(alloc);
		
		auto shader = SystemReadFileToString(alloc, StringFormat(alloc, "./Shaders/%.*s", (s32)shader_path.count, shader_path.data));
		if (shader.count == 0) return {};
		
		DxcBuffer source = {};
		source.Ptr  = shader.data;
		source.Size = shader.count;
		source.Encoding = DXC_CP_UTF8;
		
		FixedCapacityArray<const wchar_t*, 32> arguments;
		ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, shader_path).data);
		ArrayAppend(arguments, L"-E"); ArrayAppend(arguments, entry_point_names[(u32)shader_type]);
		ArrayAppend(arguments, L"-T"); ArrayAppend(arguments, target_profiles[(u32)shader_type]);
		ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, shader_type_defines[(u32)shader_type]);
		ArrayAppend(arguments, L"-Zpr");
		ArrayAppend(arguments, L"-Qstrip_reflect");
		
		IDxcResult* result = nullptr;
		if (FAILED(compiler->dxc_compiler->Compile(&source, arguments.data, (u32)arguments.count, compiler->default_include_handler, IID_PPV_ARGS(&result)))) {
			DebugAssertAlways("Internal compiler error in DXC.");
			return {};
		}
		defer{ SafeReleaseDXC(result); };
		
		
		HRESULT status = S_OK;
		if (FAILED(result->GetStatus(&status))) {
			DebugAssertAlways("Internal compiler error in DXC.");
			return {};
		}
		
		if (FAILED(status)) {
			TempAllocationScope(alloc);
			
			IDxcBlobUtf8* errors = nullptr;
			result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
			
			defer{ SafeReleaseDXC(errors); };
			
			printf("Shader compilation failed. Target: '%S'. Shader Name: '%.*s'.\n", target_profiles[(u32)shader_type], (s32)shader_path.count, shader_path.data);
			
			auto erros_utf16 = StringUtf8ToUtf16(alloc, { (char*)errors->GetStringPointer(), errors->GetStringLength() });
			WriteConsoleW(GetStdHandle(STD_ERROR_HANDLE), erros_utf16.data, (u32)erros_utf16.count, nullptr, nullptr);
		}
		
		
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode_blob), nullptr))) {
			DebugAssertAlways("Internal compiler error in DXC.");
			return {};
		}
	}
	
	
	Array<u8> bytecode;
	ArrayResize(bytecode, alloc, bytecode_blob->GetBufferSize());
	memcpy(bytecode.data, bytecode_blob->GetBufferPointer(), bytecode.count);
	
	return bytecode;
}

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc) {
	auto* compiler = NewFromAlloc(alloc, ShaderCompiler);
	
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler->dxc_compiler));
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&compiler->dxc_utils));
	compiler->dxc_utils->CreateDefaultIncludeHandler(&compiler->default_include_handler);
	
	return compiler;
}

void ReleaseShaderCompiler(ShaderCompiler* compiler) {
	compiler->default_include_handler->Release();
	compiler->dxc_utils->Release();
	compiler->dxc_compiler->Release();
}

