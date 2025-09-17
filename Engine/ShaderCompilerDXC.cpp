#include "ShaderCompiler.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApiTypes.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <SDK/dxc/inc/dxcapi.h>

compile_const u32 max_shader_count = 32;

struct ShaderPermutation {
	u64        permutation = 0;
	ShaderType shader_type = ShaderType::ComputeShader;
	
	IDxcBlob* bytecode_blob = nullptr;
};

struct ShaderPermutationTable {
	ShaderDefinition* definition = nullptr;
	Array<ShaderPermutation> permutations;
};

struct ShaderCompiler {
	IDxcCompiler3* dxc_compiler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	
	IDxcIncludeHandler* default_include_handler = nullptr;
	
	StackAllocator alloc;
	DirectoryChangeTracker* directory_change_tracker = nullptr;
	
	FixedCapacityArray<ShaderPermutationTable, max_shader_count> shaders;
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

compile_const String shader_directory_path = "./Shaders"_sl;

static IDxcBlob* CompileShaderToBlob(ShaderCompiler* compiler, StackAllocator* alloc, ShaderDefinition* definition, u64 permutation, ShaderType shader_type) {
	TempAllocationScope(alloc);
	
	auto filename = definition->filename;
	auto filepath = StringFormat(alloc, "%s/%.*s", shader_directory_path.data, (s32)filename.count, filename.data);
	
	auto shader_file = SystemReadFileToString(alloc, filepath);
	if (shader_file.data == nullptr) {
		SystemWriteToConsole(alloc, StringFormat(alloc, "Failed to open shader source file '%.*s'.\n", (s32)filepath.count, filepath.data));
		return nullptr;
	}
	
	DxcBuffer source = {};
	source.Ptr  = shader_file.data;
	source.Size = shader_file.count;
	source.Encoding = DXC_CP_UTF8;
	
	
	Array<const wchar_t*> arguments;
	ArrayReserve(arguments, alloc, 32);
	
	ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, filename).data);
	ArrayAppend(arguments, L"-E"); ArrayAppend(arguments, entry_point_names[(u32)shader_type]);
	ArrayAppend(arguments, L"-T"); ArrayAppend(arguments, target_profiles[(u32)shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, shader_type_defines[(u32)shader_type]);
	ArrayAppend(arguments, L"-Zpr");
	ArrayAppend(arguments, L"-Qstrip_reflect");
	
	for (u64 i = 0; i < 64; i += 1) {
		if (permutation & (1ull << i)) {
			ArrayAppend(arguments, alloc, L"-D");
			ArrayAppend(arguments, alloc, (wchar_t*)StringUtf8ToUtf16(alloc, definition->defines[i]).data);
		}
	}
	
	
	IDxcResult* result = nullptr;
	if (FAILED(compiler->dxc_compiler->Compile(&source, arguments.data, (u32)arguments.count, compiler->default_include_handler, IID_PPV_ARGS(&result)))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return nullptr;
	}
	defer{ SafeReleaseDXC(result); };
	
	
	HRESULT status = S_OK;
	if (FAILED(result->GetStatus(&status))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return nullptr;
	}
	
	
	IDxcBlobUtf8* error_blob = nullptr;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr);
	defer{ SafeReleaseDXC(error_blob); };
	
	String compiler_message;
	if (FAILED(status)) {
		compiler_message = StringFormat(alloc, "Shader '%.*s' failed to compile with target '%S'. Errors:\n%.*s\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type], (s32)error_blob->GetStringLength(), error_blob->GetStringPointer());
	} else if (error_blob->GetStringLength() != 0) {
		compiler_message = StringFormat(alloc, "Shader '%.*s' compiled with target '%S'. Warnings:\n%.*s\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type], (s32)error_blob->GetStringLength(), error_blob->GetStringPointer());
	} else {
		compiler_message = StringFormat(alloc, "Shader '%.*s' compiled with target '%S'.\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type]);
	}
	SystemWriteToConsole(alloc, compiler_message);
	
	if (FAILED(status)) return nullptr;
	
	
	IDxcBlob* bytecode_blob = nullptr;
	if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode_blob), nullptr))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return nullptr;
	}
	
	return bytecode_blob;
}

static ShaderPermutation* FindShaderPermutation(ShaderCompiler* compiler, ShaderDefinition* definition, u64 permutation, ShaderType shader_type) {
	auto* alloc = &compiler->alloc;
	
	auto* shader_table = definition->shader_table;
	if (shader_table == nullptr) {
		shader_table = &ArrayEmplace(compiler->shaders);
		shader_table->definition = definition;
		
		definition->shader_table = shader_table;
		ArrayReserve(shader_table->permutations, alloc, 4);
	}
	
	ShaderPermutation* shader_permutation = nullptr;
	for (auto& shader : shader_table->permutations) {
		if (shader.permutation == permutation && shader.shader_type == shader_type) {
			shader_permutation = &shader;
			break;
		}
	}
	
	if (shader_permutation == nullptr) {
		shader_permutation = &ArrayEmplace(shader_table->permutations, alloc);
		shader_permutation->permutation = permutation;
		shader_permutation->shader_type = shader_type;
	}
	
	return shader_permutation;
}

FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count> CompileShader(ShaderCompiler* compiler, ShaderDefinition* definition, u64 permutation, ShaderTypeMask shader_type_mask) {
	auto* alloc = &compiler->alloc;
	
	FixedCountArray<ArrayView<u8>, (u32)ShaderType::Count> result;
	
	for (u32 i = 0; i < (u32)ShaderType::Count; i += 1) {
		if (((u32)shader_type_mask & (1u << i)) == 0) continue;
		
		auto* shader = FindShaderPermutation(compiler, definition, permutation, (ShaderType)i);
		auto* bytecode_blob = shader->bytecode_blob;
		
		while (bytecode_blob == nullptr) {
			bytecode_blob = CompileShaderToBlob(compiler, alloc, definition, permutation, (ShaderType)i);
			
			if (bytecode_blob == nullptr) {
				SystemWriteToConsole(alloc, "Press enter to recompile.\n"_sl);
				
				FixedCapacityArray<wchar_t, 16> buffer;
				ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buffer.data, buffer.capacity, (DWORD*)&buffer.count, nullptr);
			}
		}
		
		shader->bytecode_blob = bytecode_blob;
		
		result[i].data = (u8*)bytecode_blob->GetBufferPointer();
		result[i].count = bytecode_blob->GetBufferSize();
	}
	
	return result;
}

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc) {
	auto* compiler = NewFromAlloc(alloc, ShaderCompiler);
	
	compiler->alloc = CreateStackAllocator(16 * 1024 * 1024, 64 * 1024);
	
	compiler->directory_change_tracker = CreateDirectoryChangeTracker(&compiler->alloc, shader_directory_path);
	DebugAssert(compiler->directory_change_tracker != nullptr, "Failed to create shader directory change tracker.");
	
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler->dxc_compiler));
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&compiler->dxc_utils));
	compiler->dxc_utils->CreateDefaultIncludeHandler(&compiler->default_include_handler);
	
	return compiler;
}

void ReleaseShaderCompiler(ShaderCompiler* compiler) {
	compiler->default_include_handler->Release();
	compiler->dxc_utils->Release();
	compiler->dxc_compiler->Release();
	
	ReleaseDirectoryChangeTracker(compiler->directory_change_tracker);
	
	ReleaseStackAllocator(compiler->alloc);
}

bool CheckShaderFileChanges(ShaderCompiler* compiler) {
	auto* alloc = &compiler->alloc;
	TempAllocationScope(alloc);
	
	auto changed_files = ReadDirectoryChangeEvents(alloc, compiler->directory_change_tracker);
	
	bool has_dirty_shaders = false;
	for (auto& path : changed_files) {
		ShaderPermutationTable* table = nullptr;
		for (auto& shader : compiler->shaders) {
			if (path == shader.definition->filename) {
				table = &shader;
				break;
			}
		}
		
		if (table != nullptr) {
			for (auto& permutation : table->permutations) {
				SafeReleaseDXC(permutation.bytecode_blob);
			}
			SystemWriteToConsole(alloc, StringFormat(alloc, "File Changed: %.*s\n", (s32)path.count, path.data));
			has_dirty_shaders = true;
		}
	}
	
	return has_dirty_shaders;
}

