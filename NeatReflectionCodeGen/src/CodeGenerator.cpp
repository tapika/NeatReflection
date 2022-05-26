#include "CodeGenerator.h"

#include <format>
#include <cassert>
#include <cctype>
#include <algorithm>

#include "ifc/File.h"
#include "ifc/Declaration.h"
#include "ifc/Type.h"
#include "ifc/Name.h"


CodeGenerator::CodeGenerator(ifc::File& file)
	: file(file)
{
	code.reserve(1024);
}

void CodeGenerator::write_cpp_file(std::ostream& out)
{
	assert(file.header().unit.sort() == ifc::UnitSort::Primary); // For now

	auto unit_index = file.header().unit.index;
	const auto module_name = file.get_string(ifc::TextOffset{ unit_index });
	
	scan(file.global_scope());
	
		out << std::format(
R"(#include "Neat/Reflection.h"
#include "Neat/TemplateTypeId.h"

#include <memory>

import {0};


namespace Neat::Detail
{{
	class InvokeFunctor
	{{
	public:
		InvokeFunctor(auto callable) {{ callable(); }}
	}};
	
	static InvokeFunctor neat_reflection_data_initialiser{{ [] {{
{1}
	}} }};
}})", module_name, code);

	out.flush();
}

void CodeGenerator::scan(ifc::ScopeDescriptor scope_desc)
{
	auto declarations = ifc::get_declarations(file, scope_desc);
	for (auto& declaration : declarations)
	{
		scan(declaration.index);
	}
}

void CodeGenerator::scan(ifc::DeclIndex decl)
{
	switch (decl.sort())
	{
	case ifc::DeclSort::Scope:
		scan(ifc::get_scope(file, decl));
		break;
	}
}

void CodeGenerator::scan(const ifc::ScopeDeclaration& scope_decl)
{
	switch (ifc::get_kind(scope_decl, file))
	{
	case ifc::TypeBasis::Class:
	case ifc::TypeBasis::Struct:
		render(get_user_type_name(file, scope_decl.name), scope_decl);
		break;
	case ifc::TypeBasis::Union:
		// TODO: Implement at some point
		break;
	case ifc::TypeBasis::Namespace:
		scan(file.scope_descriptors()[scope_decl.initializer]);
		break;
	}
}

void CodeGenerator::render(std::string_view type_name, const ifc::ScopeDeclaration& scope_decl)
{
	auto var_name = to_snake_case(type_name) + '_';
	auto scope_descriptor = file.scope_descriptors()[scope_decl.initializer];
	auto [fields, methods] = render_members(type_name, var_name, scope_descriptor);

	code += std::format(R"(auto& {0} = add_type({{ "{1}", get_id<{1}>() }});
{2}
{3}
{4}
)", var_name, type_name, "", fields, methods);
}

CodeGenerator::TypeMembers CodeGenerator::render_members(std::string_view type_name, std::string_view type_variable, ifc::ScopeDescriptor scope_desc)
{
	std::string fields;
	std::string methods;

	auto declarations = ifc::get_declarations(file, scope_desc);
	for (auto& decl : declarations)
	{
		switch (decl.index.sort())
		{
		case ifc::DeclSort::Field:
		{
			const auto& field = file.fields()[decl.index];
			const auto type = render_full_typename(field.type);
			const auto name = file.get_string(field.name);

			fields += std::format(R"({0}.fields.push_back(std::make_unique<FieldImpl<{1}, {2}, &{1}::{3}>>("{3}"));
)", type_variable, type_name, type, name);
			break;
		}
		case ifc::DeclSort::Method:
		{
			const auto& method = file.methods()[decl.index];
			assert(method.type.sort() == ifc::TypeSort::Method);
			const auto& method_type = file.method_types()[method.type];
			const auto return_type = render_full_typename(method_type.target);
			auto param_types = std::string{ "" };
			if (!method_type.source.is_null())
			{
				param_types = ", " + render_full_typename(method_type.source);
			}
			const auto name = get_user_type_name(file, method.name);

			methods += std::format(R"({0}.methods.push_back(std::make_unique<MethodImpl<{1}, {2}{3}>>("{4}", &{1}::{4}));
)", type_variable, type_name, return_type, param_types, name);
			break;
		}
		}
	}
	return { fields, methods };
}

std::string CodeGenerator::render_full_typename(ifc::TypeIndex type_index)
{
	switch (type_index.sort())
	{
	case ifc::TypeSort::Fundamental:
		return render_full_typename(file.fundamental_types()[type_index]);
	case ifc::TypeSort::Pointer:
		return render_full_typename(file.pointer_types()[type_index].pointee) + "*";
	case ifc::TypeSort::LvalueReference:
		return render_full_typename(file.lvalue_references()[type_index].referee) + "&";
	case ifc::TypeSort::RvalueReference:
		return render_full_typename(file.rvalue_references()[type_index].referee) + "&&";
	case ifc::TypeSort::Qualified:
		return render_full_typename(file.qualified_types()[type_index].unqualified) +
			render(file.qualified_types()[type_index].qualifiers);
	case ifc::TypeSort::Base:
		// render only the typename, no access modifiers or specifiers
		return render_full_typename(file.base_types()[type_index].type);
	case ifc::TypeSort::Placeholder:
		if (auto& elaborated_type = file.placeholder_types()[type_index].elaboration; !elaborated_type.is_null())
		{
			return render_full_typename(elaborated_type);
		}
		assert(false && "IFC doesn't contain deduced type");
		return "PLACEHOLDER_TYPE";
	case ifc::TypeSort::Tuple:
		return render_full_typename(file.tuple_types()[type_index]);

		// Currently unsupported
	case ifc::TypeSort::Expansion: // variadic pack expansion (...)
	case ifc::TypeSort::Function: // U (*)(Args...);
	case ifc::TypeSort::PointerToMember: // U (T::*);
	case ifc::TypeSort::Method: // U (T::*)(Args...);
	// case ifc::TypeSort::Array: // T t[N]; Not implemented yet in ifc-reader
	// case ifc::TypeSort::Typename: // typename T::dependant_type; Not implementet yet in ifc-reader
	case ifc::TypeSort::Decltype: // Seems to complicated to support
	case ifc::TypeSort::Forall: // Template declaration. Not used yet in MSVC
	case ifc::TypeSort::Unaligned: // __unaligned T; Partition not implemented yet in ifc-reader

		// Not planned to be supported
	case ifc::TypeSort::VendorExtension:
	case ifc::TypeSort::Tor: // Compiler generated constructor
	case ifc::TypeSort::Syntactic: // type expressed at the C++ source-level as a type-id. Typical examples include a template - id designating a specialization.
	case ifc::TypeSort::SyntaxTree: // General parse tree representation. Seems to complicated to support

	default:
		//assert(false && "Not supported yet");
		return "UNSUPPORTED_TYPE_" + std::to_string((uint32_t)type_index.sort());
	}
}

std::string CodeGenerator::render_full_typename(const ifc::FundamentalType& type)
{
	std::string rendered;
	rendered.reserve(16);

	if (type.sign == ifc::TypeSign::Unsigned)
	{
		rendered += "unsigned ";
	}

	switch (type.precision)
	{
	case ifc::TypePrecision::Default:
		break;
	case ifc::TypePrecision::Short:
		assert(type.basis == ifc::TypeBasis::Int);
		return "short";
	case ifc::TypePrecision::Long:
		rendered += "long ";
		break;
	case ifc::TypePrecision::Bit64:
		assert(type.basis == ifc::TypeBasis::Int);
		return "long long";
	case ifc::TypePrecision::Bit8:
		if (type.basis == ifc::TypeBasis::Char)
		{
			return "char8_t";
		}
	case ifc::TypePrecision::Bit16:
		if (type.basis == ifc::TypeBasis::Char)
		{
			return "char16_t";
		}
	case ifc::TypePrecision::Bit32:
		if (type.basis == ifc::TypeBasis::Char)
		{
			return "char32_t";
		}
	case ifc::TypePrecision::Bit128:
		rendered += std::format("Unsupported Bitness '{0}' ", 
			static_cast<int>(type.precision));
		break;
	}

	switch (type.basis)
	{
	case ifc::TypeBasis::Void:
		rendered += "void";
		break;
	case ifc::TypeBasis::Bool:
		rendered += "bool";
		break;
	case ifc::TypeBasis::Char:
		rendered += "char";
		break;
	case ifc::TypeBasis::Wchar_t:
		rendered += "wchar_t";
		break;
	case ifc::TypeBasis::Int:
		rendered += "int";
		break;
	case ifc::TypeBasis::Float:
		rendered += "float";
		break;
	case ifc::TypeBasis::Double:
		rendered += "double";
		break;
	default:
		rendered += std::format("fundamental type ({0})", 
			static_cast<int>(type.basis));
		break;
	}

	return rendered;
}


std::string CodeGenerator::render_full_typename(const ifc::TupleType& types)
{
	std::string rendered;
	rendered.reserve(ifc::raw_count(types.cardinality) * 15);

	bool first = true;
	for (auto& type : file.type_heap().slice(types))
	{
		if (!first)
		{
			rendered += ", ";
		}

		rendered += render_full_typename(types);

		first = false;
	}

	return rendered;
}

std::string CodeGenerator::render(ifc::Qualifiers qualifiers)
{
	using namespace std::string_view_literals;

	std::string rendered;
	rendered.reserve("const"sv.size() + "volatile"sv.size());
	
	if (ifc::has_qualifier(qualifiers, ifc::Qualifiers::Const))
	{
		rendered += "const ";
	}
	if (ifc::has_qualifier(qualifiers, ifc::Qualifiers::Volatile))
	{
		rendered += "volatile ";
	}
	if (ifc::has_qualifier(qualifiers, ifc::Qualifiers::Restrict))
	{
		// Ignored
	}

	return rendered;
}


std::string_view get_user_type_name(const ifc::File& file, ifc::NameIndex name)
{
	assert(name.sort() == ifc::NameSort::Identifier);
	return { file.get_string(ifc::TextOffset{ name.index }) };
}

std::string replace_all_copy(std::string str, std::string_view target, std::string_view replacement)
{
	size_t start_pos = 0;
	while ((start_pos = str.find(target, start_pos)) != std::string::npos) {
		str.replace(start_pos, target.length(), replacement);
		start_pos += replacement.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

std::string to_snake_case(std::string_view type_name)
{
	std::string snake_case;
	snake_case.reserve(type_name.size() + 10);

	bool previous_uppercase = true;
	for (auto c : type_name)
	{
		if (!previous_uppercase && std::isupper(c))
		{
			snake_case.push_back('_');
		}

		if (std::isalnum(c))
		{
			snake_case.push_back(std::tolower(c));
		}
		else 
		{
			snake_case.push_back('_'); // Fallback
		}

		previous_uppercase = std::isupper(c);
	}

	return snake_case;
}