#include "Basic/Basic.h"
#include "Basic/BasicString.h"
#include "Basic/BasicFiles.h"
#include "MetaprogramCommon.h"

static void GenerateVectorType(StringBuilder& builder, u32 count, String type, String suffix, ArrayView<String> ops, ArrayView<String> unary_ops) {
	auto name = StringFormat(builder.alloc, "Vec%.%"_sl, count, suffix);
	
	builder.Append("struct % {\n"_sl, name);
	builder.Indent();
	
	if (count == 2) {
		builder.Append("%0 x; %0 y;\n\n"_sl, type);
		
		builder.Append("constexpr Vec2%0() : x(0), y(0) {}\n"_sl, suffix);
		builder.Append("constexpr Vec2%0(%1 x) : x(x), y(x) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec2%0(%1 x, %1 y) : x(x), y(y) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec2%0(const Vec2%0& xy) : x(xy.x), y(xy.y) {}\n\n"_sl, suffix);
		builder.Append("template<typename T> explicit constexpr Vec2%0(const T& xy) : x((%1)xy.x), y((%1)xy.y) {}\n\n"_sl, suffix, type);
	} else if (count == 3) {
		builder.Append("union {\n"_sl);
		builder.Indent();
		
		builder.Append("struct { %0 x; %0 y; %0 z; };\n"_sl, type);
		builder.Append("Vec2% xy;\n"_sl, suffix);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("constexpr Vec3%0() : x(0), y(0), z(0) {}\n"_sl, suffix);
		builder.Append("constexpr Vec3%0(%1 x) : x(x), y(x), z(x) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec3%0(%1 x, %1 y, %1 z) : x(x), y(y), z(z) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec3%0(const Vec2%0& xy, %1 z) : x(xy.x), y(xy.y), z(z) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec3%0(const Vec3%0& xyz) : x(xyz.x), y(xyz.y), z(xyz.z) {}\n\n"_sl, suffix);
		builder.Append("template<typename T> explicit constexpr Vec3%0(const T& xyz) : x((%1)xyz.x), y((%1)xyz.y), z((%1)xyz.z) {}\n\n"_sl, suffix, type);
	} else if (count == 4) {
		builder.Append("union {\n"_sl);
		builder.Indent();
		
		builder.Append("struct { %0 x; %0 y; %0 z; %0 w; };\n"_sl, type);
		builder.Append("struct { Vec2%0 xy; Vec2%0 zw; };\n"_sl, suffix);
		builder.Append("Vec3%0 xyz;\n"_sl, suffix);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
		
		builder.Append("constexpr Vec4%0() : x(0), y(0), z(0), w(0) {}\n"_sl, suffix);
		builder.Append("constexpr Vec4%0(%1 x) : x(x), y(x), z(x), w(x) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec4%0(%1 x, %1 y, %1 z, %1 w) : x(x), y(y), z(z), w(w) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec4%0(const Vec2%0& xy, %1 z, %1 w) : x(xy.x), y(xy.y), z(z), w(w) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec4%0(const Vec2%0& xy, const Vec2%0& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}\n"_sl, suffix);
		builder.Append("constexpr Vec4%0(const Vec3%0& xyz, %1 w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}\n"_sl, suffix, type);
		builder.Append("constexpr Vec4%0(const Vec4%0& xyzw) : x(xyzw.x), y(xyzw.y), z(xyzw.z), w(xyzw.w) {}\n\n"_sl, suffix);
		builder.Append("template<typename T> explicit constexpr Vec4%0(const T& xyzw) : x((%1)xyzw.x), y((%1)xyzw.y), z((%1)xyzw.z), w((%1)xyzw.w) {}\n\n"_sl, suffix, type);
	}
	
	for (auto op : ops) {
		if (count == 2) {
			builder.Append("%0 operator%1(const %0& other) const { return %0(x %1 other.x, y %1 other.y); }\n"_sl, name, op);
			builder.Append("%0 operator%1(%2 other) const { return %0(x %1 other, y %1 other); }\n\n"_sl, name, op, type);
		} else if (count == 3) {
			builder.Append("%0 operator%1(const %0& other) const { return %0(x %1 other.x, y %1 other.y, z %1 other.z); }\n"_sl, name, op);
			builder.Append("%0 operator%1(%2 other) const { return %0(x %1 other, y %1 other, z %1 other); }\n\n"_sl, name, op, type);
		} else if (count == 4) {
			builder.Append("%0 operator%1(const %0& other) const { return %0(x %1 other.x, y %1 other.y, z %1 other.z, w %1 other.w); }\n"_sl, name, op);
			builder.Append("%0 operator%1(%2 other) const { return %0(x %1 other, y %1 other, z %1 other, w %1 other); }\n\n"_sl, name, op, type);
		}
	}
	
	for (auto op : ops) {
		if (count == 2) {
			builder.Append("%0& operator%1=(const %0& other) { x %1= other.x; y %1= other.y; return *this; }\n"_sl, name, op);
			builder.Append("%0& operator%1=(%2 other) { x %1= other; y %1= other; return *this; }\n\n"_sl, name, op, type);
		} else if (count == 3) {
			builder.Append("%0& operator%1=(const %0& other) { x %1= other.x; y %1= other.y; z %1= other.z; return *this; }\n"_sl, name, op);
			builder.Append("%0& operator%1=(%2 other) { x %1= other; y %1= other; z %1= other; return *this; }\n\n"_sl, name, op, type);
		} else if (count == 4) {
			builder.Append("%0& operator%1=(const %0& other) { x %1= other.x; y %1= other.y; z %1= other.z; w %1= other.w; return *this; }\n"_sl, name, op);
			builder.Append("%0& operator%1=(%2 other) { x %1= other; y %1= other; z %1= other; w %1= other; return *this; }\n\n"_sl, name, op, type);
		}
	}
	
	for (auto op : unary_ops) {
		if (count == 2) {
			builder.Append("%0 operator%1() const { return %0(%1.x, %1y); }\n\n"_sl, name, op);
		} else if (count == 3) {
			builder.Append("%0 operator%1() const { return %0(%1.x, %1y, %1z); }\n\n"_sl, name, op);
		} else if (count == 4) {
			builder.Append("%0 operator%1() const { return %0(%1.x, %1y, %1z, %1w); }\n\n"_sl, name, op);
		}
	}
	
	builder.Append("%& operator[](u32 index) { return (&x)[index]; }\n"_sl, type);
	builder.Append("const %& operator[](u32 index) const { return (&x)[index]; }\n\n"_sl, type);
	
	builder.Append("compile_const u64 count = %;\n"_sl, count);
	builder.Append("compile_const u64 capacity = %;\n"_sl, count);
	builder.Append("using ValueType = %;\n"_sl, type);
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
}

static void GenerateMatrixType(StringBuilder& builder, u32 rows, u32 cols, String type, String suffix) {
	auto name = StringFormat(builder.alloc, "Mat%.x%.%"_sl, rows, cols, suffix);
	auto row  = StringFormat(builder.alloc, "Vec%.%"_sl, cols, suffix);
	auto col  = StringFormat(builder.alloc, "Vec%.%"_sl, rows, suffix);
	
	builder.Append("struct % {\n"_sl, name);
	builder.Indent();
	
	for (u32 i = 0; i < rows; i += 1) {
		builder.Append("% r%;\n"_sl, row, i);
	}
	builder.Append("\n"_sl);
	
	builder.Append("%& operator[](u32 index) { return (&r0)[index]; }\n"_sl, row);
	builder.Append("const %& operator[](u32 index) const { return (&r0)[index]; }\n\n"_sl, row);
	
	builder.Append("compile_const u32 element_count = %;\n"_sl, rows);
	
	builder.Unindent();
	builder.Append("};\n\n"_sl);
	
	{
		builder.Append("inline % operator*(const %& m, const %& v) {\n"_sl, col, name, row);
		builder.Indent();
		
		builder.Append("% result;\n"_sl, col);
		for (u32 i = 0; i < rows; i += 1) {
			builder.Append("result[%] = Dot(m[%], v);\n"_sl, i, i);
		}
		builder.Append("return result;\n"_sl);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	{
		builder.Append("inline % operator*(const %& v, const %& m) {\n"_sl, row, col, name);
		builder.Indent();
		builder.Append("return "_sl);
		
		StringBuilder line_builder;
		line_builder.alloc = builder.alloc;
		
		for (u32 i = 0; i < cols; i += 1) {
			if (i != 0) line_builder.Append(" + "_sl);
			line_builder.Append("(m[%] * v[%])"_sl, i, i);
		}
		line_builder.Append(";\n"_sl);
		
		builder.AppendBuilder(line_builder);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	{
		builder.Append("inline %0 operator*(const %0& lh, const %0& rh) {\n"_sl, name);
		builder.Indent();
		builder.Append("% result;\n"_sl, name);
		
		for (u32 i = 0; i < cols; i += 1) {
			builder.Append("result[%] = "_sl, i);
			
			StringBuilder line_builder;
			line_builder.alloc = builder.alloc;
			
			for (u32 j = 0; j < cols; j += 1) {
				if (j != 0) line_builder.Append(" + "_sl);
				line_builder.Append("(rh[%1] * lh[%0][%1])"_sl, i, j);
			}
			line_builder.Append(";\n"_sl);
			
			builder.AppendBuilder(line_builder);
		}
		builder.Append("return result;\n"_sl);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
	
	if (rows == cols) {
		builder.Append("inline %0 Transpose(const %0& m) {\n"_sl, name);
		builder.Indent();
		
		builder.Append("% result;\n"_sl, name);
		for (u32 i = 0; i < rows; i += 1) {
			for (u32 j = 0; j < cols; j += 1) {
				builder.Append("result[%0][%1] = m[%1][%0];\n"_sl, i, j);
			}
		}
		builder.Append("return result;\n"_sl);
		
		builder.Unindent();
		builder.Append("};\n\n"_sl);
	}
}

static void GenerateVectorFunctions(StringBuilder& builder, u32 count, String type, String suffix) {
	auto name = StringFormat(builder.alloc, "Vec%.%"_sl, count, suffix);
	
	if (count == 2) {
		builder.Append("inline %0 Cross(const %1& lh, const %1& rh) { return lh.x * rh.y - lh.y * rh.x; }\n"_sl, type, name);
	} else if (count == 3) {
		builder.Append("inline %0 Cross(const %0& lh, const %0& rh) {\n"_sl, name);
		builder.Indent();
		
		builder.Append("% result;\n"_sl, name);
		builder.Append("result.x = lh.y * rh.z - lh.z * rh.y;\n"_sl);
		builder.Append("result.y = lh.z * rh.x - lh.x * rh.z;\n"_sl);
		builder.Append("result.z = lh.x * rh.y - lh.y * rh.x;\n"_sl);
		builder.Append("return result;\n"_sl);
		
		builder.Unindent();
		builder.Append("}\n\n"_sl);
	}
	
	if (count == 2) {
		builder.Append("inline %0 Dot(const %1& lh, const %1& rh) { return lh.x * rh.x + lh.y * rh.y; }\n"_sl, type, name);
	} else if (count == 3) {
		builder.Append("inline %0 Dot(const %1& lh, const %1& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z; }\n"_sl, type, name);
	} else if (count == 4) {
		builder.Append("inline %0 Dot(const %1& lh, const %1& rh) { return lh.x * rh.x + lh.y * rh.y + lh.z * rh.z + lh.w * rh.w; }\n"_sl, type, name);
	}
	
	builder.Append("inline %0 LengthSquare(const %1& v) { return Dot(v, v); }\n"_sl, type, name);
	builder.Append("inline %0 Length(const %1& v) { return sqrt%2(Dot(v, v)); }\n"_sl, type, name, suffix);
	builder.Append("inline %0 Normalize(const %0& v) { return v * (1.%1 / Length(v)); }\n\n"_sl, name, suffix);
}

void GenerateMathLibrary(StackAllocator* alloc) {
	StringBuilder builder;
	builder.alloc = alloc;
	
	builder.Append("#pragma once\n"_sl);
	builder.Append("#include \"Basic.h\"\n\n"_sl);
	builder.Append("#include <math.h>\n\n"_sl);
	
	builder.Append("namespace Math {\n\n"_sl);
	builder.Indent();
	
	
	FixedCountArray<String, 10> uint_vector_ops;
	uint_vector_ops[0] = "+"_sl;
	uint_vector_ops[1] = "-"_sl;
	uint_vector_ops[2] = "*"_sl;
	uint_vector_ops[3] = "/"_sl;
	uint_vector_ops[4] = "%"_sl;
	uint_vector_ops[5] = "&"_sl;
	uint_vector_ops[6] = "|"_sl;
	uint_vector_ops[7] = "^"_sl;
	uint_vector_ops[8] = "<<"_sl;
	uint_vector_ops[9] = ">>"_sl;
	
	FixedCountArray<String, 1> uint_vector_unary_ops;
	uint_vector_unary_ops[0] = "~"_sl;
	
	for (u32 i = 2; i <= 4; i += 1) {
		GenerateVectorType(builder, i, "u32"_sl, "u32"_sl, uint_vector_ops, uint_vector_unary_ops);
	}
	
	FixedCountArray<String, 4> float_vector_ops;
	float_vector_ops[0] = "+"_sl;
	float_vector_ops[1] = "-"_sl;
	float_vector_ops[2] = "*"_sl;
	float_vector_ops[3] = "/"_sl;
	
	FixedCountArray<String, 1> float_vector_unary_ops;
	float_vector_unary_ops[0] = "-"_sl;
	
	for (u32 i = 2; i <= 4; i += 1) {
		GenerateVectorType(builder, i, "float"_sl, "f"_sl, float_vector_ops, float_vector_unary_ops);
		GenerateVectorFunctions(builder, i, "float"_sl, "f"_sl);
	}
	
	GenerateMatrixType(builder, 4, 4, "float"_sl, "f"_sl);
	GenerateMatrixType(builder, 3, 4, "float"_sl, "f"_sl);
	GenerateMatrixType(builder, 3, 3, "float"_sl, "f"_sl);
	
	
	builder.Unindent();
	builder.Append("} // namespace Math\n\n"_sl);
	
	WriteGeneratedFile(alloc, "Basic/BasicMathGenerated.h"_sl, builder.ToString());
}
