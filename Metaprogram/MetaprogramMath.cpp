#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"

compile_const String vector_components = "xyzw"_sl;

static void GenerateVectorType(StringBuilder& builder, u32 count, String type, String suffix, ArrayView<String> ops) {
	auto name = StringFormat(builder.alloc, "Vec%u%s", count, suffix.data);
	
	builder.Append("struct %s {\n", name.data);
	builder.Indent();
	
	if (count == 2) {
		builder.Append("%s x; %s y;\n\n", type.data, type.data);
		
		builder.Append("constexpr Vec2%s() : x(0), y(0) {}\n", suffix.data);
		builder.Append("constexpr Vec2%s(%s x) : x(x), y(x) {}\n", suffix.data, type.data);
		builder.Append("constexpr Vec2%s(%s x, %s y) : x(x), y(y) {}\n", suffix.data, type.data, type.data);
		builder.Append("constexpr Vec2%s(const Vec2%s& xy) : x(xy.x), y(xy.y) {}\n\n", suffix.data, suffix.data);
		builder.Append("template<typename T> explicit constexpr Vec2%s(const T& xy) : x((%s)xy.x), y((%s)xy.y) {}\n\n", suffix.data, type.data, type.data, type.data);
	} else if (count == 3) {
		builder.Append("union {\n");
		builder.Indent();
		
		builder.Append("struct { %s x; %s y; %s z; };\n", type.data, type.data, type.data);
		builder.Append("Vec2%s xy;\n", suffix.data);
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
		
		builder.Append("constexpr Vec3%s() : x(0), y(0), z(0) {}\n", suffix.data);
		builder.Append("constexpr Vec3%s(%s x) : x(x), y(x), z(x) {}\n", suffix.data, type.data);
		builder.Append("constexpr Vec3%s(%s x, %s y, %s z) : x(x), y(y), z(z) {}\n", suffix.data, type.data, type.data, type.data);
		builder.Append("constexpr Vec3%s(const Vec2%s& xy, %s z) : x(xy.x), y(xy.y), z(z) {}\n", suffix.data, suffix.data, type.data);
		builder.Append("constexpr Vec3%s(const Vec3%s& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}\n\n", suffix.data, suffix.data);
		builder.Append("template<typename T> explicit constexpr Vec3%s(const T& xyz) : x((%s)xyz.x), y((%s)xyz.y), z((%s)xyz.z) {}\n\n", suffix.data, type.data, type.data, type.data);
	} else if (count == 4) {
		builder.Append("union {\n");
		builder.Indent();
		
		builder.Append("struct { %s x; %s y; %s z; %s w; };\n", type.data, type.data, type.data, type.data);
		builder.Append("struct { Vec2%s xy; Vec2%s zw; };\n", suffix.data, suffix.data);
		builder.Append("Vec3%s xyz;\n", suffix.data);
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
		
		builder.Append("constexpr Vec4%s() : x(0), y(0), z(0), w(0) {}\n", suffix.data);
		builder.Append("constexpr Vec4%s(%s x) : x(x), y(x), z(x), w(x) {}\n", suffix.data, type.data);
		builder.Append("constexpr Vec4%s(%s x, %s y, %s z, %s w) : x(x), y(y), z(z), w(w) {}\n", suffix.data, type.data, type.data, type.data, type.data);
		builder.Append("constexpr Vec4%s(const Vec2%s& xy, %s z, %s w) : x(xy.x), y(xy.y), z(z), w(w) {}\n", suffix.data, suffix.data, type.data, type.data);
		builder.Append("constexpr Vec4%s(const Vec2%s& xy, const Vec2%s& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}\n", suffix.data, suffix.data, suffix.data);
		builder.Append("constexpr Vec4%s(const Vec3%s& xyz, %s w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}\n", suffix.data, suffix.data, type.data);
		builder.Append("constexpr Vec4%s(const Vec4%s& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}\n\n", suffix.data, suffix.data);
		builder.Append("template<typename T> explicit constexpr Vec4%s(const T& xyzw) : x((%s)xyzw.x), y((%s)xyzw.y), z((%s)xyzw.z), w((%s)xyzw.w) {}\n\n", suffix.data, type.data, type.data, type.data, type.data);
	}
	
	for (u64 i = 0; i < ops.count; i += 1) {
		auto op = ops[i].data;
		if (count == 2) {
			builder.Append("%s operator%s(const %s& other) const { return %s(x %s other.x, y %s other.y); }\n", name.data, op, name.data, name.data, op, op);
			builder.Append("%s operator%s(%s other) const { return %s(x %s other, y %s other); }\n\n", name.data, op, type.data, name.data, op, op);
		} else if (count == 3) {
			builder.Append("%s operator%s(const %s& other) const { return %s(x %s other.x, y %s other.y, z %s other.z); }\n", name.data, op, name.data, name.data, op, op, op);
			builder.Append("%s operator%s(%s other) const { return %s(x %s other, y %s other, z %s other); }\n\n", name.data, op, type.data, name.data, op, op, op);
		} else if (count == 4) {
			builder.Append("%s operator%s(const %s& other) const { return %s(x %s other.x, y %s other.y, z %s other.z, w %s other.w); }\n", name.data, op, name.data, name.data, op, op, op, op);
			builder.Append("%s operator%s(%s other) const { return %s(x %s other, y %s other, z %s other, w %s other); }\n\n", name.data, op, type.data, name.data, op, op, op, op);
		}
	}
	
	builder.Append("%s& operator[](u32 index) { return (&x)[index]; }\n", type.data);
	builder.Append("const %s& operator[](u32 index) const { return (&x)[index]; }\n\n", type.data);
	
	builder.Append("compile_const u32 element_count = %u;\n", count);
	
	builder.Unindent();
	builder.AppendUnformatted("};\n\n"_sl);
}

static void GenerateMatrixType(StringBuilder& builder, u32 rows, u32 cols, String type, String suffix) {
	auto name = StringFormat(builder.alloc, "Mat%ux%u%s", rows, cols, suffix.data);
	auto row  = StringFormat(builder.alloc, "Vec%u%s", cols, suffix.data);
	auto col  = StringFormat(builder.alloc, "Vec%u%s", rows, suffix.data);
	
	builder.Append("struct %s {\n", name.data);
	builder.Indent();
	
	builder.Append("%s rows[%u];\n\n", row.data, rows);
	
	builder.Append("%s& operator[](u32 index) { return rows[index]; }\n", row.data);
	builder.Append("const %s& operator[](u32 index) const { return rows[index]; }\n\n", row.data);
	
	builder.Append("compile_const u32 element_count = %u;\n", rows);
	
	builder.Unindent();
	builder.AppendUnformatted("};\n\n"_sl);
	
	{
		builder.Append("inline %s operator*(const %s& m, const %s& v) {\n", col.data, name.data, row.data);
		builder.Indent();
		
		builder.Append("%s result;\n", col.data);
		for (u32 i = 0; i < rows; i += 1) {
			builder.Append("result[%u] = Dot(m[%u], v);\n", i, i);
		}
		builder.AppendUnformatted("return result;\n"_sl);
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	{
		builder.Append("inline %s operator*(const %s& v, const %s& m) {\n", row.data, col.data, name.data);
		builder.Indent();
		
		builder.Append("%s result;\n", row.data);
		for (u32 i = 0; i < cols; i += 1) {
			builder.Append("result = result + (m[%u] * v[%u]);\n", i, i);
		}
		builder.AppendUnformatted("return result;\n"_sl);
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
	
	if (rows == cols) {
		builder.Append("inline %s Transpose(const %s& m) {\n", name.data, name.data);
		builder.Indent();
		
		builder.Append("%s result;\n", name.data);
		for (u32 i = 0; i < rows; i += 1) {
			for (u32 j = 0; j < cols; j += 1) {
				builder.Append("result[%u][%u] = m[%u][%u];\n", i, j, j, i);
			}
		}
		builder.AppendUnformatted("return result;\n"_sl);
		
		builder.Unindent();
		builder.AppendUnformatted("};\n\n"_sl);
	}
}

static void GenerateVectorFunctions(StringBuilder& builder, u32 count, String type, String suffix) {
	auto name = StringFormat(builder.alloc, "Vec%u%s", count, suffix.data);
	
	if (count == 2) {
		builder.Append("inline %s Cross(const %s& lh, const %s& rh) { return lh.x * rh.y - lh.y * rh.x; }\n", type.data, name.data, name.data);
	} else if (count == 3) {
		builder.Append("inline %s Cross(const %s& lh, const %s& rh) {\n", name.data, name.data, name.data);
		builder.Indent();
		
		builder.Append("%s result;\n", name.data);
		builder.Append("result.x = lh.y * rh.z - lh.z * rh.y;\n");
		builder.Append("result.y = lh.z * rh.x - lh.x * rh.z;\n");
		builder.Append("result.z = lh.x * rh.y - lh.y * rh.x;\n");
		builder.Append("return result;\n");
		
		builder.Unindent();
		builder.AppendUnformatted("}\n\n"_sl);
	}
	
	if (count == 2) {
		builder.Append("inline %s Dot(const %s& lh, const %s& rh) { return lh.x * rh.x + lh.y * rh.y; }\n", type.data, name.data, name.data);
	} else if (count == 3) {
		builder.Append("inline %s Dot(const %s& lh, const %s& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z; }\n", type.data, name.data, name.data);
	} else if (count == 4) {
		builder.Append("inline %s Dot(const %s& lh, const %s& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z + lh.w * rh.w; }\n", type.data, name.data, name.data);
	}
	
	builder.Append("inline %s LengthSquare(const %s& v) { return Dot(v, v); }\n", type.data, name.data);
	builder.Append("inline %s Length(const %s& v) { return sqrt%s(Dot(v, v)); }\n\n", type.data, name.data, suffix.data);
}

void GenerateMathLibrary(StackAllocator* alloc) {
	StringBuilder builder;
	builder.alloc = alloc;
	
	builder.Append("#pragma once\n");
	builder.Append("#include \"Basic.h\"\n\n");
	builder.Append("#include <math.h>\n\n");
	
	builder.AppendUnformatted("namespace Math {\n\n"_sl);
	builder.Indent();
	
	
	FixedCountArray<String, 10> integer_vector_ops;
	integer_vector_ops[0] = "+"_sl;
	integer_vector_ops[1] = "-"_sl;
	integer_vector_ops[2] = "*"_sl;
	integer_vector_ops[3] = "/"_sl;
	integer_vector_ops[4] = "%"_sl;
	integer_vector_ops[5] = "&"_sl;
	integer_vector_ops[6] = "|"_sl;
	integer_vector_ops[7] = "^"_sl;
	integer_vector_ops[8] = "<<"_sl;
	integer_vector_ops[9] = ">>"_sl;
	
	for (u32 i = 2; i <= 4; i += 1) {
		GenerateVectorType(builder, i, "u32"_sl, "u32"_sl, integer_vector_ops);
	}
	
	FixedCountArray<String, 4> float_vector_ops;
	float_vector_ops[0] = "+"_sl;
	float_vector_ops[1] = "-"_sl;
	float_vector_ops[2] = "*"_sl;
	float_vector_ops[3] = "/"_sl;
	
	for (u32 i = 2; i <= 4; i += 1) {
		GenerateVectorType(builder, i, "float"_sl, "f"_sl, float_vector_ops);
		GenerateVectorFunctions(builder, i, "float"_sl, "f"_sl);
	}
	
	GenerateMatrixType(builder, 4, 4, "float"_sl, "f"_sl);
	GenerateMatrixType(builder, 3, 4, "float"_sl, "f"_sl);
	GenerateMatrixType(builder, 3, 3, "float"_sl, "f"_sl);
	
	
	builder.Unindent();
	builder.AppendUnformatted("} // namespace Math\n\n"_sl);
	
	auto output_filepath = "./Basic/BasicMathGenerated.h"_sl;
	auto output_file = SystemOpenFile(alloc, output_filepath, OpenFileFlags::Write);
	if (output_file.handle == nullptr) {
		SystemWriteToConsole(alloc, "Failed to open output file '%s'.\n", output_filepath.data);
		SystemExitProcess(1);
	}
	
	auto file_string = builder.ToString();
	SystemWriteFile(output_file, file_string.data, file_string.count, 0);
	SystemCloseFile(output_file);
}
