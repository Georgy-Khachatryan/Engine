#include "ShaderCompiler.h"
#include "Basic/BasicMemory.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicBitArray.h"
#include "Basic/BasicFiles.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicSaveLoad.h"

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

union ShaderPermutationKey {
	struct {
		u64        permutation;
		ShaderType shader_type : 4;
		u32        root_signature_index : 28;
		ShaderID   shader_id;
	};
	u64 data[2] = { 0, 0 };
	
	bool operator== (const ShaderPermutationKey& other) { return data[0] == other.data[0] && data[1] == other.data[1]; }
};
static_assert(sizeof(ShaderPermutationKey) == 16, "Incorrect ShaderPermutationKey size.");

static u64 ComputeHash(const ShaderPermutationKey& key) { return ComputeHash64(key.data[0], key.data[1]); }


struct ShaderCompiler {
	IDxcCompiler3* dxc_compiler = nullptr;
	IDxcUtils* dxc_utils = nullptr;
	IDxcIncludeHandler* dxc_default_include_handler = nullptr;
	
	DirectoryChangeTracker* directory_change_tracker = nullptr;
	
	ArrayView<String>             root_signature_filenames;
	ArrayView<ShaderDefinition>   shader_definitions;
	ArrayView<PipelineDefinition> pipeline_definitions;
	
	Array<PipelineShaderIndices> pipeline_shader_indices;
	Array<ShaderPermutation>     shaders;
	HashTable<ShaderPermutationKey, u32> shader_permutation_table;
	
	HeapAllocator heap; // Persistent allocator (shader bytecode, hashed sources, etc.), usage is restricted to CreateShaderPermutation(...).
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
	L"ms_6_8",
};

compile_const wchar_t* entry_point_names[(u32)ShaderType::Count] = {
	L"MainCS",
	L"MainVS",
	L"MainPS",
	L"MainMS",
};

compile_const wchar_t* shader_type_defines[(u32)ShaderType::Count] = {
	L"COMPUTE_SHADER",
	L"VERTEX_SHADER",
	L"PIXEL_SHADER",
	L"MESH_SHADER",
};

compile_const String shader_type_names[(u32)ShaderType::Count] = {
	"CS"_sl,
	"VS"_sl,
	"PS"_sl,
	"MS"_sl,
};

compile_const String shader_directory_path = "./Shaders"_sl;
compile_const String shader_cache_filepath = "./Build/ShaderCache.bin"_sl;

struct ShaderSourceFile {
	String contents;
	u64 hash = 0;
};

static ShaderSourceFile ReadShaderSourceFile(StackAllocator* alloc, String filename) {
	auto filepath = StringStartsWith(filename, "SDK"_sl) ? filename : StringFormat(alloc, "%/%"_sl, shader_directory_path, filename);
	
	ShaderSourceFile shader_file;
	shader_file.contents = SystemReadFileToString(alloc, filepath);
	shader_file.hash     = ComputeHash(shader_file.contents);
	
	return shader_file;
}

static void CreateShaderPermutation(HeapAllocator* heap, ShaderPermutation& shader, ArrayView<HashedShaderSourceFile> hashed_source_files, ArrayView<u8> bytecode_blob) {
	for (auto& hashed_file : shader.hashed_source_files) {
		heap->Deallocate(hashed_file.filename.data);
	}
	shader.hashed_source_files.count = 0;
	
	ArrayReserve(shader.hashed_source_files, heap, hashed_source_files.count);
	for (auto& src_hashed_file : hashed_source_files) {
		auto& dst_hashed_file = ArrayEmplace(shader.hashed_source_files);
		dst_hashed_file.filename = StringCopy(heap, src_hashed_file.filename);
		dst_hashed_file.hash     = src_hashed_file.hash;
	}
	
	ArrayResize(shader.bytecode_blob, heap, bytecode_blob.count);
	memcpy(shader.bytecode_blob.data, bytecode_blob.data, bytecode_blob.count);
	
	shader.is_dirty = false;
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

static bool CompileShaderToBlob(ShaderCompiler* compiler, StackAllocator* alloc, const ShaderPermutationKey& key, ShaderPermutation& shader) {
	ProfilerScope("CompileShaderToBlob");
	TempAllocationScope(alloc);
	
	auto& definition = compiler->shader_definitions[key.shader_id.index];
	auto filename    = definition.filename;
	auto shader_file = ReadShaderSourceFile(alloc, filename);
	
	if (shader_file.contents.data == nullptr) {
		SystemWriteToConsole(alloc, "Failed to open shader source file '%'.\n"_sl, filename);
		return false;
	}
	
	
	IncludeHandler include_handler;
	include_handler.alloc = alloc;
	include_handler.dxc_default_include_handler = compiler->dxc_default_include_handler;
	include_handler.dxc_utils = compiler->dxc_utils;
	ArrayReserve(include_handler.hashed_source_files, alloc, 16);
	ArrayAppend(include_handler.hashed_source_files, { filename, shader_file.hash });
	
	auto root_signature_filename = compiler->root_signature_filenames[key.root_signature_index];
	auto root_signature_filepath = StringFormat(alloc, "ROOT_SIGNATURE_FILEPATH=\"Generated/%\""_sl, root_signature_filename);
	
	Array<const wchar_t*> arguments;
	ArrayReserve(arguments, alloc, 14 + CountSetBits(key.permutation) * 2);
	
	ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, filename).data);
	ArrayAppend(arguments, L"-E"); ArrayAppend(arguments, entry_point_names[(u32)key.shader_type]);
	ArrayAppend(arguments, L"-T"); ArrayAppend(arguments, target_profiles[(u32)key.shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, shader_type_defines[(u32)key.shader_type]);
	ArrayAppend(arguments, L"-D"); ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, root_signature_filepath).data);
	ArrayAppend(arguments, L"-Zpr");
	ArrayAppend(arguments, L"-enable-16bit-types");
	
#define DXC_ENABLE_DEBUG_INFO 1
#if DXC_ENABLE_DEBUG_INFO
	ArrayAppend(arguments, L"-Zi"); // In the debug and dev builds embed PDBs and keep reflection to help in debugging with PIX.
	ArrayAppend(arguments, L"-Qembed_debug");
#else // !DXC_ENABLE_DEBUG_INFO
	ArrayAppend(arguments, L"-Qstrip_reflect"); // TODO: Remove both PDBs and reflection when they're not needed.
#endif // !DXC_ENABLE_DEBUG_INFO
	
	for (u64 i : BitScanLow(key.permutation)) {
		ArrayAppend(arguments, L"-D");
		ArrayAppend(arguments, (wchar_t*)StringUtf8ToUtf16(alloc, definition.defines[i]).data);
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
	
	auto permutation_name = GetShaderPermutationName(alloc, definition, key.permutation);
	auto shader_type_name = shader_type_names[(u32)key.shader_type];
	auto dxc_error_message = String{ (char*)error_blob->GetStringPointer(), error_blob->GetStringLength() };
	
	String compiler_message;
	if (FAILED(status)) {
		compiler_message = StringFormat(alloc, "Shader '%' failed to compile with target '%'. Errors:\n\x1B[31m%\x1B[0m\n"_sl, permutation_name, shader_type_name, dxc_error_message);
	} else if (dxc_error_message.count != 0) {
		compiler_message = StringFormat(alloc, "Shader '%' compiled with target '%'. Warnings:\n\x1B[33m%\x1B[0m\n"_sl, permutation_name, shader_type_name, dxc_error_message);
	} else {
		compiler_message = StringFormat(alloc, "Shader '%' compiled with target '%'.\n"_sl, permutation_name, shader_type_name);
	}
	SystemWriteToConsole(compiler_message);
	
	if (FAILED(status)) return false;
	
	
	IDxcBlob* bytecode_blob = nullptr;
	if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode_blob), nullptr))) {
		DebugAssertAlways("Internal compiler error in DXC.");
		return false;
	}
	defer{ SafeReleaseDXC(bytecode_blob); };
	
	CreateShaderPermutation(&compiler->heap, shader, include_handler.hashed_source_files, { (u8*)bytecode_blob->GetBufferPointer(), bytecode_blob->GetBufferSize() });
	
	return true;
}


ArrayView<u64> CompileDirtyShaderPermutations(ShaderCompiler* compiler, StackAllocator* alloc) {
	ProfilerScope("CompileDirtyShaderPermutations");
	
	Array<u64> compiled_shader_mask;
	ArrayResizeMemset(compiled_shader_mask, alloc, DivideAndRoundUp((u32)compiler->shader_permutation_table.count, 64u));
	
	for (auto& [key, shader_index] : compiler->shader_permutation_table) {
		auto& shader = compiler->shaders[shader_index];
		bool shader_compiled = false;
		
		bool should_recompile = shader.is_dirty;
		while (should_recompile) {
			shader_compiled = CompileShaderToBlob(compiler, alloc, key, shader);
			should_recompile &= (shader_compiled == false) && (shader.bytecode_blob.data == nullptr);
			
			if (shader.bytecode_blob.data == nullptr) {
				SystemWriteToConsole("Press enter to recompile.\n"_sl);
				
				FixedCapacityArray<wchar_t, 16> buffer;
				ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buffer.data, buffer.capacity, (DWORD*)&buffer.count, nullptr);
			}
		}
		
		if (shader_compiled) {
			BitArraySetBit(compiled_shader_mask, shader_index);
		}
	}
	
	return compiled_shader_mask;
}

PipelineShaderBytecode GetShadersForPipelineIndex(ShaderCompiler* compiler, u64 pipeline_definition_index, ArrayView<u64> compiled_shader_mask) {
	auto& pipeline_definition     = compiler->pipeline_definitions[pipeline_definition_index];
	auto& pipeline_shader_indices = compiler->pipeline_shader_indices[pipeline_definition_index];
	
	PipelineShaderBytecode result;
	for (u32 shader_type_index : BitScanLow32((u32)pipeline_definition.shader_type_mask)) {
		u32 shader_index = pipeline_shader_indices[shader_type_index];
		result.is_dirty |= BitArrayTestBit(compiled_shader_mask, shader_index);
		
		auto& shader = compiler->shaders[shader_index];
		result.bytecode[shader_type_index] = shader.bytecode_blob;
	}
	return result;
}

String GetShaderPermutationName(StackAllocator* alloc, const ShaderDefinition& definition, u64 permutation) {
	FixedCapacityArray<String, 65> strings;
	ArrayAppend(strings, definition.filename);
	
	for (u64 i : BitScanLow(permutation)) {
		ArrayAppend(strings, definition.defines[i]);
	}
	
	return StringJoin(alloc, strings, "-"_sl);
}

ShaderCompiler* CreateShaderCompiler(StackAllocator* alloc, ArrayView<String> root_signature_filenames, ArrayView<ShaderDefinition> shader_definitions, ArrayView<PipelineDefinition> pipeline_definitions) {
	ProfilerScope("CreateShaderCompiler");
	
	auto* compiler = NewFromAlloc(alloc, ShaderCompiler);
	
	compiler->root_signature_filenames = root_signature_filenames;
	compiler->shader_definitions       = shader_definitions;
	compiler->pipeline_definitions     = pipeline_definitions;
	
	ArrayResizeMemset(compiler->pipeline_shader_indices, alloc, pipeline_definitions.count);
	
	// Deduplicate shaders that are used across multiple pipelines.
	HashTable<ShaderPermutationKey, u32> shader_permutation_table;
	HashTableReserve(shader_permutation_table, alloc, pipeline_definitions.count * (u32)ShaderType::Count);
	
	for (u64 i = 0; i < pipeline_definitions.count; i += 1) {
		auto& pipeline_definition = pipeline_definitions[i];
		auto& indices = compiler->pipeline_shader_indices[i];
		
		for (u32 shader_type_index : BitScanLow32((u32)pipeline_definition.shader_type_mask)) {
			ShaderPermutationKey key;
			key.permutation = pipeline_definition.permutation;
			key.shader_type = (ShaderType)shader_type_index;
			key.root_signature_index = pipeline_definition.root_signature_id.index;
			key.shader_id   = pipeline_definition.shader_id;
			
			auto [element, is_added] = HashTableAddOrFind(shader_permutation_table, key, (u32)shader_permutation_table.count);
			indices[shader_type_index] = element->value;
		}
	}
	compiler->shader_permutation_table = shader_permutation_table;
	
	ArrayResize(compiler->shaders, alloc, shader_permutation_table.count);
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

struct ShaderCache {
	ArrayView<String>           root_signature_filenames;
	ArrayView<ShaderDefinition> shader_definitions;
	Array<ShaderPermutation>    shaders;
	HashTable<ShaderPermutationKey, u32> shader_permutation_table;
};

// TODO: Simplify shader cache validation.
static void ValidateShaderCache(StackAllocator* alloc, ShaderCache& old_cache, ShaderCache& new_cache) {
	ProfilerScope("ValidateShaderCache");
	
	HashTable<String, u32> shader_definition_table;
	HashTableReserve(shader_definition_table, alloc, new_cache.shader_definitions.count);
	for (u32 i = 0; i < new_cache.shader_definitions.count; i += 1) {
		HashTableAddOrFind(shader_definition_table, new_cache.shader_definitions[i].filename, i);
	}
	
	HashTable<String, u32> root_signature_filename_table;
	HashTableReserve(root_signature_filename_table, alloc, new_cache.root_signature_filenames.count);
	for (u32 i = 0; i < new_cache.root_signature_filenames.count; i += 1) {
		HashTableAddOrFind(root_signature_filename_table, new_cache.root_signature_filenames[i], i);
	}
	
	// Valudate shader definitions.
	for (auto& definition : old_cache.shader_definitions) {
		auto* new_shader_id = HashTableFind(shader_definition_table, definition.filename);
		if (new_shader_id == nullptr) continue;
		
		auto& new_definition = new_cache.shader_definitions[new_shader_id->value];
		
		bool matches = (definition.defines.count == new_definition.defines.count);
		for (u32 i = 0; i < definition.defines.count && matches; i += 1) {
			matches &= (definition.defines[i] == new_definition.defines[i]);
		}
		
		if (matches == false) new_shader_id->value = u32_max;
	}
	
	// Validate and remap shader permutation keys.
	for (auto& [key, shader_index] : old_cache.shader_permutation_table) {
		auto* new_shader_id            = HashTableFind(shader_definition_table,       old_cache.shader_definitions[key.shader_id.index].filename);
		auto* new_root_signature_index = HashTableFind(root_signature_filename_table, old_cache.root_signature_filenames[key.root_signature_index]);
		
		if (new_shader_id && new_root_signature_index && new_shader_id->value != u32_max) {
			key.shader_id.index      = new_shader_id->value;
			key.root_signature_index = new_root_signature_index->value;
		} else {
			shader_index = u32_max;
		}
	}
	
	
	HashTable<String, u64> all_hashed_source_files;
	HashTableReserve(all_hashed_source_files, alloc, 128);
	
	// Validate shader sources.
	for (auto& shader : old_cache.shaders) {
		bool is_dirty_shader = false;
		for (auto& file : shader.hashed_source_files) {
			auto [hashed_source_file, is_added] = HashTableAddOrFind(all_hashed_source_files, file.filename, 0llu);
			if (is_added) {
				TempAllocationScope(alloc);
				
				auto shader_file = ReadShaderSourceFile(alloc, file.filename);
				hashed_source_file->value = shader_file.hash;
			}
			
			if (file.hash != hashed_source_file->value) {
				is_dirty_shader = true;
				break;
			}
		}
		shader.is_dirty |= is_dirty_shader;
	}
}


static void SaveLoad(SaveLoadBuffer& buffer, ShaderDefinition& definition, u64 version = 0) {
	SaveLoad(buffer, definition.filename);
	SaveLoad(buffer, definition.defines);
}

static void SaveLoad(SaveLoadBuffer& buffer, HashedShaderSourceFile& hashed_source_file, u64 version = 0) {
	SaveLoad(buffer, hashed_source_file.filename);
	SaveLoad(buffer, hashed_source_file.hash);
}

static void SaveLoad(SaveLoadBuffer& buffer, ShaderPermutation& shader, u64 version = 0) {
	SaveLoad(buffer, shader.bytecode_blob);
	SaveLoad(buffer, shader.hashed_source_files);
}

static void SaveLoad(SaveLoadBuffer& buffer, HashTableElement<ShaderPermutationKey, u32>& element, u64 version = 0) {
	SaveLoad(buffer, element.key.data[0]);
	SaveLoad(buffer, element.key.data[1]);
	SaveLoad(buffer, element.value);
}

static void SaveLoad(SaveLoadBuffer& buffer, ShaderCache& cache) {
	SaveLoad(buffer, cache.root_signature_filenames);
	SaveLoad(buffer, cache.shader_definitions);
	SaveLoad(buffer, cache.shader_permutation_table);
	SaveLoad(buffer, cache.shaders);
}

void SaveLoadShaderCache(ShaderCompiler* compiler, StackAllocator* alloc, bool should_load_shader_cache) {
	ProfilerScope("SaveLoadShaderCache");
	TempAllocationScope(alloc);
	
	SaveLoadBuffer buffer;
	if (!OpenSaveLoadBuffer(alloc, shader_cache_filepath, should_load_shader_cache, buffer)) return;
	defer{ CloseSaveLoadBuffer(buffer); };
	
	ShaderCache old_cache;
	
	ShaderCache new_cache;
	new_cache.root_signature_filenames = compiler->root_signature_filenames;
	new_cache.shader_definitions       = compiler->shader_definitions;
	new_cache.shader_permutation_table = compiler->shader_permutation_table;
	new_cache.shaders                  = compiler->shaders;
	SaveLoad(buffer, should_load_shader_cache ? old_cache : new_cache);
	
	if (should_load_shader_cache) {
		ValidateShaderCache(alloc, old_cache, new_cache);
		
		auto* heap = &compiler->heap;
		for (auto& [key, shader_index] : old_cache.shader_permutation_table) {
			if (shader_index == u32_max || old_cache.shaders[shader_index].is_dirty) continue;
			
			auto* new_shader_entry = HashTableFind(new_cache.shader_permutation_table, key);
			if (new_shader_entry == nullptr) continue;
			
			auto& cached_shader = old_cache.shaders[shader_index];
			CreateShaderPermutation(heap, new_cache.shaders[new_shader_entry->value], cached_shader.hashed_source_files, cached_shader.bytecode_blob);
		}
	}
}
