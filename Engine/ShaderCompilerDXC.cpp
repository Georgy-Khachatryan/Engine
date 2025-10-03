#include "ShaderCompiler.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "GraphicsApi/GraphicsApiTypes.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <SDK/dxc/inc/dxcapi.h>


struct HashedShaderSourceFile {
	String filename;
	u64 hash = 0;
};

struct ShaderPermutation {
	u64        permutation = 0;
	ShaderType shader_type = ShaderType::ComputeShader;
	bool      shader_dirty = false;
	
	Array<u8> bytecode_blob;
	Array<HashedShaderSourceFile> hashed_source_files;
};

struct ShaderPermutationTable {
	ShaderDefinition* definition = nullptr;
	Array<ShaderPermutation> permutations;
};

struct ShaderCompiler {
	IDxcCompiler3* dxc_compiler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	IDxcIncludeHandler* dxc_default_include_handler = nullptr;
	
	DirectoryChangeTracker* directory_change_tracker = nullptr;
	
	compile_const u32 max_shader_count = 32;
	FixedCapacityArray<ShaderPermutationTable, max_shader_count> shaders;
	
	HeapAllocator heap; // Persistent allocator (shader bytecode, shader permutations, hashed sources, etc.)
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


struct ShaderSourceFile {
	String contents;
	u64 hash = 0;
};

static ShaderSourceFile ReadShaderSourceFile(StackAllocator* alloc, String filename) {
	auto filepath = StringFormat(alloc, "%s/%.*s", shader_directory_path.data, (s32)filename.count, filename.data);
	
	ShaderSourceFile shader_file;
	shader_file.contents = SystemReadFileToString(alloc, filepath);
	shader_file.hash     = ComputeHash(shader_file.contents);
	
	return shader_file;
}

struct IncludeHandler : IDxcIncludeHandler {
	StackAllocator* alloc = nullptr;
	IDxcIncludeHandler* dxc_default_include_handler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	
	Array<HashedShaderSourceFile> hashed_source_files;
	
	HRESULT LoadSource(const wchar_t* filename_utf16, IDxcBlob** source_file_blob) {
		*source_file_blob = nullptr;
		
		auto filename = StringUtf16ToUtf8(alloc, { (u16*)filename_utf16, wcslen(filename_utf16) });
		while (filename.count != 0 && (filename[0] == '.' || filename[0] == '/' || filename[0] == '\\')) {
			filename.data  += 1;
			filename.count -= 1;
		}
		
		auto shader_file = ReadShaderSourceFile(alloc, filename);
		if (shader_file.contents.data == nullptr) return ERROR_FILE_NOT_FOUND;
		
		IDxcBlobEncoding* output_blob = nullptr; // Blobs created using CreateBlobFromPinned get cleaned up by the compiler.
		auto result = dxc_utils->CreateBlobFromPinned(shader_file.contents.data, (u32)shader_file.contents.count, DXC_CP_UTF8, &output_blob);
		if (FAILED(result)) return result;
		
		*source_file_blob = output_blob;
		ArrayAppend(hashed_source_files, alloc, { filename, shader_file.hash });
		
		return S_OK;
	}
	
	HRESULT QueryInterface(REFIID riid, void** object) {
		return dxc_default_include_handler->QueryInterface(riid, object);
	}
	
	ULONG AddRef()  { return 0; }
	ULONG Release() { return 0; }
};

static bool CompileShaderToBlob(ShaderCompiler* compiler, StackAllocator* alloc, ShaderDefinition* definition, u64 permutation, ShaderType shader_type, ShaderPermutation* shader_permutation) {
	TempAllocationScope(alloc);
	
	auto filename = definition->filename;
	auto shader_file = ReadShaderSourceFile(alloc, filename);
	
	if (shader_file.contents.data == nullptr) {
		SystemWriteToConsole(alloc, "Failed to open shader source file '%.*s'.\n", (s32)filename.count, filename.data);
		return false;
	}
	
	
	IncludeHandler include_handler;
	include_handler.alloc = alloc;
	include_handler.dxc_default_include_handler = compiler->dxc_default_include_handler;
	include_handler.dxc_utils = compiler->dxc_utils;
	ArrayReserve(include_handler.hashed_source_files, alloc, 16);
	ArrayAppend(include_handler.hashed_source_files, { filename, shader_file.hash });
	
	
	Array<const wchar_t*> arguments;
	ArrayReserve(arguments, alloc, 10 + CountSetBits(permutation) * 2);
	
	ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, filename).data);
	ArrayAppend(arguments, L"-E"); ArrayAppend(arguments, entry_point_names[(u32)shader_type]);
	ArrayAppend(arguments, L"-T"); ArrayAppend(arguments, target_profiles[(u32)shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, shader_type_defines[(u32)shader_type]);
	ArrayAppend(arguments, L"-Zpr");
	ArrayAppend(arguments, L"-Qstrip_reflect");
	ArrayAppend(arguments, L"-enable-16bit-types");
	
	for (u64 i : BitScanLow(permutation)) {
		ArrayAppend(arguments, L"-D");
		ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, definition->defines[i]).data);
	}
	
	
	DxcBuffer source = {};
	source.Ptr  = shader_file.contents.data;
	source.Size = shader_file.contents.count;
	source.Encoding = DXC_CP_UTF8;
	
	IDxcResult* result = nullptr;
	if (FAILED(compiler->dxc_compiler->Compile(&source, arguments.data, (u32)arguments.count, &include_handler, IID_PPV_ARGS(&result)))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return false;
	}
	defer{ SafeReleaseDXC(result); };
	
	
	HRESULT status = S_OK;
	if (FAILED(result->GetStatus(&status))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return false;
	}
	
	
	IDxcBlobUtf8* error_blob = nullptr;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr);
	defer{ SafeReleaseDXC(error_blob); };
	
	String compiler_message;
	if (FAILED(status)) {
		compiler_message = StringFormat(alloc, "Shader '%.*s' failed to compile with target '%S'. Errors:\n\x1B[31m%.*s\x1B[0m\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type], (s32)error_blob->GetStringLength(), error_blob->GetStringPointer());
	} else if (error_blob->GetStringLength() != 0) {
		compiler_message = StringFormat(alloc, "Shader '%.*s' compiled with target '%S'. Warnings:\n\x1B[33m%.*s\x1B[0m\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type], (s32)error_blob->GetStringLength(), error_blob->GetStringPointer());
	} else {
		compiler_message = StringFormat(alloc, "Shader '%.*s' compiled with target '%S'.\n", (s32)filename.count, filename.data, target_profiles[(u32)shader_type]);
	}
	SystemWriteToConsole(compiler_message);
	
	if (FAILED(status)) return false;
	
	
	IDxcBlob* bytecode_blob = nullptr;
	if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode_blob), nullptr))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return false;
	}
	defer{ SafeReleaseDXC(bytecode_blob); };
	
	
	auto* heap = &compiler->heap;
	for (auto& hashed_file : shader_permutation->hashed_source_files) {
		heap->Deallocate(hashed_file.filename.data);
	}
	shader_permutation->hashed_source_files.count = 0;
	
	ArrayReserve(shader_permutation->hashed_source_files, heap, include_handler.hashed_source_files.count);
	for (auto& src_hashed_file : include_handler.hashed_source_files) {
		auto& dst_hashed_file = ArrayEmplace(shader_permutation->hashed_source_files);
		dst_hashed_file.filename = StringCopy(heap, src_hashed_file.filename);
		dst_hashed_file.hash     = src_hashed_file.hash;
	}
	
	ArrayResize(shader_permutation->bytecode_blob, heap, bytecode_blob->GetBufferSize());
	memcpy(shader_permutation->bytecode_blob.data, bytecode_blob->GetBufferPointer(), shader_permutation->bytecode_blob.count);
	shader_permutation->shader_dirty = false;
	
	return true;
}

static ShaderPermutation* FindShaderPermutation(ShaderCompiler* compiler, ShaderDefinition* definition, u64 permutation, ShaderType shader_type) {
	auto* heap = &compiler->heap;
	
	auto* shader_table = definition->shader_table;
	if (shader_table == nullptr) {
		shader_table = &ArrayEmplace(compiler->shaders);
		shader_table->definition = definition;
		
		definition->shader_table = shader_table;
		ArrayReserve(shader_table->permutations, heap, 4);
	}
	
	ShaderPermutation* shader_permutation = nullptr;
	for (auto& shader : shader_table->permutations) {
		if (shader.permutation == permutation && shader.shader_type == shader_type) {
			shader_permutation = &shader;
			break;
		}
	}
	
	if (shader_permutation == nullptr) {
		shader_permutation = &ArrayEmplace(shader_table->permutations, heap);
		shader_permutation->permutation = permutation;
		shader_permutation->shader_type = shader_type;
		shader_permutation->shader_dirty = true;
	}
	
	return shader_permutation;
}

ShaderBytecode CompileShader(ShaderCompiler* compiler, StackAllocator* alloc, ShaderDefinition* definition, u64 permutation, ShaderTypeMask shader_type_mask) {
	ShaderBytecode result;
	
	for (u32 i : BitScanLow32((u32)shader_type_mask)) {
		auto* shader = FindShaderPermutation(compiler, definition, permutation, (ShaderType)i);
		
		bool should_recompile = shader->shader_dirty;
		while (should_recompile) {
			if (CompileShaderToBlob(compiler, alloc, definition, permutation, (ShaderType)i, shader) || shader->bytecode_blob.data != nullptr) {
				should_recompile = false;
			} else {
				SystemWriteToConsole("Press enter to recompile.\n"_sl);
				
				FixedCapacityArray<wchar_t, 16> buffer;
				ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buffer.data, buffer.capacity, (DWORD*)&buffer.count, nullptr);
			}
		}
		
		result[i] = shader->bytecode_blob;
	}
	
	return result;
}

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc) {
	auto* compiler = NewFromAlloc(alloc, ShaderCompiler);
	
	compiler->heap = CreateHeapAllocator(2 * 1024 * 1024);
	
	compiler->directory_change_tracker = CreateDirectoryChangeTracker(alloc, shader_directory_path);
	DebugAssert(compiler->directory_change_tracker != nullptr, "Failed to create shader directory change tracker.");
	
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler->dxc_compiler));
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&compiler->dxc_utils));
	compiler->dxc_utils->CreateDefaultIncludeHandler(&compiler->dxc_default_include_handler);
	
	return compiler;
}

void ReleaseShaderCompiler(ShaderCompiler* compiler) {
	compiler->dxc_default_include_handler->Release();
	compiler->dxc_utils->Release();
	compiler->dxc_compiler->Release();
	
	ReleaseDirectoryChangeTracker(compiler->directory_change_tracker);
	
	ReleaseHeapAllocator(compiler->heap);
}

bool CheckShaderFileChanges(ShaderCompiler* compiler, StackAllocator* alloc) {
	TempAllocationScope(alloc);
	
	auto changed_files = ReadDirectoryChangeEvents(alloc, compiler->directory_change_tracker);
	if (changed_files.count == 0) return false;
	
	HashTable<String, u64> changed_file_hashes;
	HashTableReserve(changed_file_hashes, alloc, changed_files.count);
	
	for (auto& filename : changed_files) {
		TempAllocationScope(alloc);
		
		auto shader_file = ReadShaderSourceFile(alloc, filename);
		HashTableAddOrFind(changed_file_hashes, filename, shader_file.hash);
	}
	
	bool has_dirty_shaders = false;
	for (auto& shader : compiler->shaders) {
		for (auto& permutation : shader.permutations) {
			bool is_dirty_permutation = false;
			for (auto& source_file : permutation.hashed_source_files) {
				auto* changed_file_hash = HashTableFind(changed_file_hashes, source_file.filename);
				if (changed_file_hash == nullptr || source_file.hash == changed_file_hash->value) continue;
				
				is_dirty_permutation = true;
				break;
			}
			
			permutation.shader_dirty |= is_dirty_permutation;
			has_dirty_shaders |= permutation.shader_dirty;
		}
	}
	
	return has_dirty_shaders;
}

