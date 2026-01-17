#include "Basic/Basic.h"
#include "Basic/BasicHashTable.h"
#include "Basic/BasicFiles.h"
#include "GraphicsApi/GraphicsApiTypes.h"
#include "MetaprogramSystems.h"
#include "MetaprogramCommon.h"

struct ShaderDefinitionData {
	String filename;
	String shader_name;
	u32 define_count = 0;
};

static void GenerateCodeForShaderDefinition(StackAllocator* alloc, Array<ShaderDefinitionData>& shader_definitions, StringBuilder& builder, TypeInfoEnum* type_info_enum) {
	auto name = type_info_enum->name;
	
	u32 define_count = 0;
	if (type_info_enum->fields.count != 0) {
		builder.Append("static String shader_defines_%[] = {\n"_sl, name);
		builder.Indent();
		
		for (auto& field : type_info_enum->fields) {
			if (CountSetBits(field.value) != 1) continue;
			
			if (FirstBitLow(field.value) != define_count) {
				ReportError(alloc, field.source_location, "Out of order shader definition '%' in shader '%'. Position in enum: '%', Value: '1u << %'."_sl, field.name, name, define_count, FirstBitLow(field.value));
			}
			
			u64 underscore_count = 0;
			for (u64 i = 0; i < field.name.count - 1; i += 1) {
				char c0 = field.name[i + 0];
				char c1 = field.name[i + 1];
				
				if (CharIsLowerCase(c0) && CharIsUpperCase(c1)) {
					underscore_count += 1;
				}
			}
			
			auto string = StringAllocate(alloc, field.name.count + underscore_count);
			
			u64 offset = 0;
			for (u64 i = 0; i < field.name.count; i += 1) {
				char c0 = field.name[i + 0];
				string[offset++] = CharToUpperCase(c0);
				
				if (i + 1 < field.name.count && CharIsLowerCase(c0) && CharIsUpperCase(field.name[i + 1])) {
					string[offset++] = '_';
				}
			}
			
			builder.Append("\"%\"_sl,\n"_sl, string);
			define_count += 1;
		}
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	auto* note = FindNote<Meta::ShaderName>(type_info_enum);
	
	ShaderDefinitionData shader_definition;
	shader_definition.filename     = note->filename;
	shader_definition.shader_name  = name;
	shader_definition.define_count = define_count;
	ArrayAppend(shader_definitions, alloc, shader_definition);
}

void WriteCodeForShaderDefinitions(StackAllocator* alloc, ArrayView<TypeInfoEnum*> shader_definition_type_infos) {
	StringBuilder builder;
	builder.alloc = alloc;
	builder.Append("#include \"Basic/Basic.h\"\n"_sl);
	builder.Append("#include \"Basic/BasicString.h\"\n"_sl);
	builder.Append("#include \"Renderer/RenderPasses.h\"\n\n"_sl);
	
	Array<ShaderDefinitionData> shader_definitions;
	for (auto* type_info : shader_definition_type_infos) {
		GenerateCodeForShaderDefinition(alloc, shader_definitions, builder, type_info);
	}
	
	for (u64 i = 0; i < shader_definitions.count; i += 1) {
		builder.Append("ShaderID %ID = { % };\n"_sl, shader_definitions[i].shader_name, (u32)i);
	}
	builder.Append("\n"_sl);
	
	
	builder.Append("static ShaderDefinition shader_definition_table_internal[] = {\n"_sl);
	builder.Indent();
	
	for (auto& definition : shader_definitions) {
		if (definition.define_count != 0) {
			builder.Append("{ \"%\"_sl, ArrayView<String>{ shader_defines_%, % } },\n"_sl, definition.filename, definition.shader_name, definition.define_count);
		} else {
			builder.Append("{ \"%\"_sl, ArrayView<String>{} },\n"_sl, definition.filename);
		}
	}
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	builder.Append("ArrayView<ShaderDefinition> shader_definition_table = { shader_definition_table_internal, % };\n\n"_sl, shader_definitions.count);
	
	WriteGeneratedFile(alloc, "Renderer/Generated/ShaderDefinitions.cpp"_sl, builder.ToString());
}
