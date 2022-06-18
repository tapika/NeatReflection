#include "catch2/catch_all.hpp"
#include "Neat/Reflection.h"

#include <string_view>
#include <string>
#include <vector>

import TestModule1;


template<typename T>
void test_contains_type()
{
	using namespace std::string_view_literals;

	Neat::Type* base_type_by_name = Neat::get_type("MyBaseStruct"sv);
	Neat::Type* base_type_by_id = Neat::get_type(Neat::get_id<MyBaseStruct>());
	Neat::Type* base_type_by_template = Neat::get_type<MyBaseStruct>();

	CHECK(base_type_by_name != nullptr);
	CHECK(base_type_by_id != nullptr);
	CHECK(base_type_by_template != nullptr);

	CHECK(base_type_by_name == base_type_by_id); // Check if they are all equal
	CHECK(base_type_by_id == base_type_by_template);
}

TEST_CASE("Contains types")
{
	test_contains_type<MyBaseStruct>();
	test_contains_type<MyStruct>();
}

// Attributes avoided, not supported yet
void check_field(const Neat::Field& field, Neat::TemplateTypeId object_type, Neat::TemplateTypeId type, std::string_view name)
{
	CHECK(field.object_type == object_type);
	CHECK(field.type == type);
	CHECK(field.name == name);
}

// Attributes avoided, not suported yet
void check_method(const Neat::Method& method, Neat::TemplateTypeId object_type, Neat::TemplateTypeId return_type, std::string_view name, 
	std::vector<Neat::TemplateTypeId> argument_types)
{
	CHECK(method.object_type == object_type);
	CHECK(method.return_type == return_type);
	CHECK(method.name == name);
	CHECK(method.argument_types == argument_types);
}

TEST_CASE("Types have correct data")
{
	using namespace std::string_view_literals;

	SECTION("MyBaseStruct") {
		Neat::Type* base_type = Neat::get_type("MyBaseStruct"sv); // get the Id afterwards, so search by name
		REQUIRE(base_type != nullptr);

		CHECK(base_type->name == "MyBaseStruct");
		CHECK(base_type->id == Neat::get_id<MyBaseStruct>());
		CHECK(base_type->bases.empty());
		REQUIRE(base_type->fields.size() == 1);
		check_field(base_type->fields[0], base_type->id, Neat::get_id<int>(), "health");
		REQUIRE(base_type->methods.size() == 0);
	}
	
	SECTION("MyStruct") {
		Neat::Type* type = Neat::get_type(Neat::get_id<MyStruct>()); // get the Id beforehand
		REQUIRE(type != nullptr);

		CHECK(type->name == "MyStruct");
		CHECK(type->id == Neat::get_id<MyStruct>());
		const std::vector type_expected_bases{ Neat::get_id<MyBaseStruct>() };
		//CHECK(type->bases == type_expected_bases); // TODO: Fix bases
		REQUIRE(type->fields.size() == 1);
		check_field(type->fields[0], type->id, Neat::get_id<double>(), "damage");
		REQUIRE(type->methods.size() == 3);
		check_method(type->methods[0], type->id, Neat::get_id<void>(), "helper_function", {});
		check_method(type->methods[1], type->id, Neat::get_id<void>(), "argumented_function", { Neat::get_id<int>(), Neat::get_id<int>() });
		check_method(type->methods[2], type->id, Neat::get_id<int>(), "get_42", {});
	}
}

TEST_CASE("Read field values")
{
	SECTION("MyBaseStruct") {
		MyBaseStruct my_struct{ .health = 7 };

		Neat::Type* type = Neat::get_type<MyBaseStruct>();
		REQUIRE(type != nullptr);

		REQUIRE(!type->fields.empty());
		auto& field = type->fields[0];
		REQUIRE(field.name == "health");

		auto value = field.get_value(&my_struct);
		REQUIRE(value.has_value());
		auto value_int = std::any_cast<int>(&value);
		REQUIRE(value_int != nullptr);
		
		REQUIRE(*value_int == 7);
	}
	
	SECTION("MyStruct") {
		MyStruct my_struct{ .damage = 42.0 };

		Neat::Type* type = Neat::get_type<MyStruct>();
		REQUIRE(type != nullptr);

		REQUIRE(!type->fields.empty());
		auto& field = type->fields[0];
		REQUIRE(field.name == "damage");

		auto value = field.get_value(&my_struct);
		REQUIRE(value.has_value());
		auto value_double = std::any_cast<double>(&value);
		REQUIRE(value_double != nullptr);

		REQUIRE(*value_double == Catch::Approx(42.0));
	}
}

TEST_CASE("Write field values")
{
	SECTION("MyBaseStruct") {
		MyBaseStruct my_struct{ .health = 0 };

		Neat::Type* type = Neat::get_type<MyBaseStruct>();
		REQUIRE(type != nullptr);

		REQUIRE(!type->fields.empty());
		auto& field = type->fields[0];
		REQUIRE(field.name == "health");

		REQUIRE(my_struct.health == 0);

		field.set_value(&my_struct, 7);

		REQUIRE(my_struct.health == 7);
	}

	SECTION("MyStruct") {
		MyStruct my_struct{ .damage = 0.0 };

		Neat::Type* type = Neat::get_type<MyStruct>();
		REQUIRE(type != nullptr);

		REQUIRE(!type->fields.empty());
		auto& field = type->fields[0];
		REQUIRE(field.name == "damage");

		REQUIRE(my_struct.damage == Catch::Approx(0.0));

		field.set_value(&my_struct, 42.0);

		REQUIRE(my_struct.damage == Catch::Approx(42.0));
	}
}

TEST_CASE("Invoke method")
{
	MyStruct my_struct{ .damage = -5.0 };

	Neat::Type* type = Neat::get_type<MyStruct>();
	REQUIRE(type != nullptr);

	REQUIRE(type->methods.size() == 3);
	auto& method = type->methods[2];
	REQUIRE(method.name == "get_42");

	auto value = method.invoke(&my_struct, {});
	REQUIRE(value.has_value());
	auto value_int = std::any_cast<int>(&value);
	REQUIRE(value_int != nullptr);

	REQUIRE(*value_int == 42);
}