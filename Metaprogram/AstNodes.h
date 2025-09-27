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
	
	Count
};

struct AstNode;
struct AstNodeNotes;
struct AstNodeCodeBlock;
struct AstNodeDeclaration;
struct AstNodeStruct;


struct AstNode {
	AstNodeType type = AstNodeType::None;
};

struct AstNoteInfo {
	String type_name;
	String expression;
};

struct AstNodeNotes : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Notes;
	
	ArrayView<AstNoteInfo> notes;
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
	AstNode* type_declaration = nullptr;
	
	AstNodeDeclarationType declaration_type = AstNodeDeclarationType::None;
};

struct AstNodeCodeBlock : AstNode {
	compile_const AstNodeType my_type = AstNodeType::CodeBlock;
	
	ArrayView<AstNodeDeclaration*> declarations;
};

struct AstNodeStruct : AstNode {
	compile_const AstNodeType my_type = AstNodeType::Struct;
	
	String name;
	
	AstNodeCodeBlock* code_block = nullptr;
	AstNodeNotes* notes = nullptr;
};


template<typename T>
inline T* AstNew(StackAllocator* alloc) {
	auto* node = NewFromAlloc(alloc, T);
	node->type = T::my_type;
	return node;
}

