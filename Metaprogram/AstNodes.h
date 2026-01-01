#pragma once
#include "Basic/Basic.h"
#include "Basic/BasicArray.h"
#include "Basic/BasicMemory.h"


enum struct AstNodeType : u32 {
	None        = 0,
	Notes       = 1,
	CodeBlock   = 2,
	Declaration = 3,
	Struct      = 4,
	Enum        = 5,
	
	Count
};

struct AstNode;
struct AstNodeNotes;
struct AstNodeCodeBlock;
struct AstNodeDeclaration;
struct AstNodeStruct;
struct AstNodeEnum;


struct AstNode {
	AstNodeType type = AstNodeType::None;
};

struct AstNoteInfo {
	String expression;
	u64 source_location = 0;
};

struct AstNodeNotes : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Notes;
	
	ArrayView<AstNoteInfo> notes;
	u32 note_offset = 0;
};

enum struct AstNodeDeclarationType : u32 {
	None     = 0,
	Variable = 1,
	Constant = 2,
	Typename = 3,
	
	Count
};

struct AstNodeDeclaration : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Declaration;
	
	String name;
	u64 source_location = 0;
	
	AstNode* type_declaration = nullptr;
	
	AstNodeDeclarationType declaration_type = AstNodeDeclarationType::None;
	AstNodeNotes* notes = nullptr;
};

struct AstNodeCodeBlock : AstNode {
	compile_const AstNodeType my_type = AstNodeType::CodeBlock;
	
	String namespace_path;
	String template_expression;
	
	ArrayView<AstNodeDeclaration*> declarations;
};

struct AstNodeStruct : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Struct;
	
	String name;
	u64 source_location = 0;
	
	AstNodeCodeBlock* template_code_block = nullptr;
	AstNodeCodeBlock* code_block = nullptr;
};

struct AstNodeEnum : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Enum;
	
	String name;
	u64 source_location = 0;
	
	String underlying_type;
	AstNodeCodeBlock* code_block = nullptr;
};


template<typename T>
inline T* AstNew(StackAllocator* alloc) {
	auto* node = NewFromAlloc(alloc, T);
	node->type = T::my_type;
	return node;
}

