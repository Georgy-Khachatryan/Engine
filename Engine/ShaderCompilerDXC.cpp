#include "ShaderCompiler.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "GraphicsApi/GraphicsApi.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <SDK/dxc/inc/dxcapi.h>


struct HashedShaderSourceFile {
	String filename;
	u64 hash = 0;
};

struct ShaderPermutation {
	Array<u8> bytecode_blob;
	Array<HashedShaderSourceFile> hashed_source_files;
	bool is_dirty = false;
};

using PipelineShaderIndices = FixedCountArray<u32, (u32)ShaderType::Count>;

struct ShaderCompiler {
	IDxcCompiler3* dxc_compiler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	IDxcIncludeHandler* dxc_default_include_handler = nullptr;
	
	DirectoryChangeTracker* directory_change_tracker = nullptr;
	
	ArrayView<String>             root_signature_filenames;
	ArrayView<ShaderDefinition*>  shader_definitions;
	ArrayView<PipelineDefinition> pipeline_definitions;
	
	Array<PipelineShaderIndices> pipeline_shader_indices;
	Array<ShaderPermutation>     shaders;
	
	HeapAllocator heap; // Persistent allocator (shader bytecode, hashed sources, etc.)
};

template<typename ResourceT>
static void SafeReleaseDXC(ResourceT*& resource) {
	if (resource) resource->Release();
	resource = nullptr;
}

compile_const wchar_t* target_profiles[(u32)ShaderType::Count] = {
	L"cs_6_8",
	L"vs_6_8",
	L"ps_6_8",
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

static bool CompileShaderToBlob(ShaderCompiler* compiler, StackAllocator* alloc, PipelineDefinition& pipeline_definition, ShaderType shader_type, ShaderPermutation& shader) {
	TempAllocationScope(alloc);
	
	auto* definition = compiler->shader_definitions[pipeline_definition.shader_id.index];
	u64  permutation = pipeline_definition.permutation;
	
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
	
	auto root_signature_filename = compiler->root_signature_filenames[pipeline_definition.root_signature_index];
	auto root_signature_filepath = StringFormat(alloc, "ROOT_SIGNATURE_FILEPATH=\"Generated/%.*s\"", (s32)root_signature_filename.count, root_signature_filename.data);
	
	Array<const wchar_t*> arguments;
	ArrayReserve(arguments, alloc, 12 + CountSetBits(permutation) * 2);
	
	ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, filename).data);
	ArrayAppend(arguments, L"-E"); ArrayAppend(arguments, entry_point_names[(u32)shader_type]);
	ArrayAppend(arguments, L"-T"); ArrayAppend(arguments, target_profiles[(u32)shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, shader_type_defines[(u32)shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, root_signature_filepath).data);
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
	for (auto& hashed_file : shader.hashed_source_files) {
		heap->Deallocate(hashed_file.filename.data);
	}
	shader.hashed_source_files.count = 0;
	
	ArrayReserve(shader.hashed_source_files, heap, include_handler.hashed_source_files.count);
	for (auto& src_hashed_file : include_handler.hashed_source_files) {
		auto& dst_hashed_file = ArrayEmplace(shader.hashed_source_files);
		dst_hashed_file.filename = StringCopy(heap, src_hashed_file.filename);
		dst_hashed_file.hash     = src_hashed_file.hash;
	}
	
	ArrayResize(shader.bytecode_blob, heap, bytecode_blob->GetBufferSize());
	memcpy(shader.bytecode_blob.data, bytecode_blob->GetBufferPointer(), shader.bytecode_blob.count);
	shader.is_dirty = false;
	
	return true;
}


ShaderBytecode CompileShadersForPipelineIndex(ShaderCompiler* compiler, StackAllocator* alloc, u64 pipeline_definition_index) {
	ShaderBytecode result;
	
	auto& pipeline_definition = compiler->pipeline_definitions[pipeline_definition_index];
	auto& shader_indices      = compiler->pipeline_shader_indices[pipeline_definition_index];
	
	for (u32 shader_type_index : BitScanLow32((u32)pipeline_definition.shader_type_mask)) {
		auto& shader = compiler->shaders[shader_indices[shader_type_index]];
		
		bool should_recompile = shader.is_dirty;
		while (should_recompile) {
			if (CompileShaderToBlob(compiler, alloc, pipeline_definition, (ShaderType)shader_type_index, shader) || shader.bytecode_blob.data != nullptr) {
				should_recompile = false;
			} else {
				SystemWriteToConsole("Press enter to recompile.\n"_sl);
				
				FixedCapacityArray<wchar_t, 16> buffer;
				ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buffer.data, buffer.capacity, (DWORD*)&buffer.count, nullptr);
			}
		}
		
		result[shader_type_index] = shader.bytecode_blob;
	}
	
	return result;
}

union ShaderPermutationKey {
	struct {
		u64        permutation;
		ShaderType shader_type;
		ShaderID   shader_id;
	};
	u64 data[2] = { 0, 0 };
	
	bool operator== (const ShaderPermutationKey& other) { return data[0] == other.data[0] && data[1] == other.data[1]; }
};
static_assert(sizeof(ShaderPermutationKey) == 16, "Incorrect ShaderPermutationKey size.");

static u64 ComputeHash(const ShaderPermutationKey& key) { return ComputeHash64(key.data[0], key.data[1]); }

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc, ArrayView<String> root_signature_filenames, ArrayView<ShaderDefinition*> shader_definitions, ArrayView<PipelineDefinition> pipeline_definitions) {
	auto* compiler = NewFromAlloc(alloc, ShaderCompiler);
	
	compiler->root_signature_filenames = root_signature_filenames;
	compiler->shader_definitions       = shader_definitions;
	compiler->pipeline_definitions     = pipeline_definitions;
	
	ArrayResizeMemset(compiler->pipeline_shader_indices, alloc, pipeline_definitions.count);
	
	// Deduplicate shaders that are used across multiple pipelines.
	HashTable<ShaderPermutationKey, u32> shader_indices;
	HashTableReserve(shader_indices, alloc, pipeline_definitions.count * (u32)ShaderType::Count);
	
	for (u64 i = 0; i < pipeline_definitions.count; i += 1) {
		auto& pipeline_definition = pipeline_definitions[i];
		auto& indices = compiler->pipeline_shader_indices[i];
		
		for (u32 shader_type_index : BitScanLow32((u32)pipeline_definition.shader_type_mask)) {
			ShaderPermutationKey key;
			key.permutation = pipeline_definition.permutation;
			key.shader_type = (ShaderType)shader_type_index;
			key.shader_id   = pipeline_definition.shader_id;
			
			auto [element, is_added] = HashTableAddOrFind(shader_indices, key, (u32)shader_indices.count);
			indices[shader_type_index] = element->value;
		}
	}
	
	ArrayResize(compiler->shaders, alloc, shader_indices.count);
	for (auto& shader : compiler->shaders) shader.is_dirty = true;
	
	
	compiler->heap = CreateHeapAllocator(512 * 1024);
	
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
		bool is_dirty_shader = false;
		for (auto& source_file : shader.hashed_source_files) {
			auto* changed_file_hash = HashTableFind(changed_file_hashes, source_file.filename);
			if (changed_file_hash == nullptr || source_file.hash == changed_file_hash->value) continue;
			
			is_dirty_shader = true;
			break;
		}
		
		shader.is_dirty |= is_dirty_shader;
		has_dirty_shaders |= shader.is_dirty;
	}
	
	return has_dirty_shaders;
}

