#pragma once

#include "otcv.h"
#include <cassert>

struct Std140AlignmentType {
	enum class InlineType {
		Vec4,
		Vec3,
		Float,
		Uint,
		Mat4
	};
	Std140AlignmentType& add(InlineType type, const std::string& name, uint32_t array_count = 1);

	Std140AlignmentType& add(Std140AlignmentType& custom_type, const std::string& name, uint32_t array_count = 1);

	struct Range {
		VkDeviceSize offset;
		VkDeviceSize stride;
		VkDeviceSize size;
	};

	std::vector<Range> _field_ranges;
	std::map<std::string, size_t> _field_name_range_map; // maps name to _field_ranges index

	std::map<std::string, InlineType> _field_name_inline_type_map;
	std::map<std::string, Std140AlignmentType> _field_name_custom_type_map;

	VkDeviceSize _total_size = 0;
};

// UBO
//struct StaticUBOBuilder {
//
//	struct Range {
//		VkDeviceSize offset;
//		VkDeviceSize size;
//		VkDeviceSize padding;
//	};
//
//	struct CustomType {
//		CustomType(StaticUBOBuilder* parent);
//		CustomType& add_field(const std::string& name, Types type, uint32_t array_count = 1);
//		CustomType& add_field(const std::string& name, const std::string& type, uint32_t array_count = 1);
//		StaticUBOBuilder& end();
//
//		StaticUBOBuilder* _parent = nullptr;
//		std::map<std::string, Range> _name_range_map;
//		Range _total_range;
//	};
//
//	StaticUBOBuilder();
//	~StaticUBOBuilder();
//	CustomType& custom_type(const std::string& type_name);
//	StaticUBOBuilder& add_field(Types type, const std::string& name, uint32_t array_count = 1);
//	StaticUBOBuilder& add_field(const std::string& custom_type, uint32_t array_count = 1);
//	StaticUBOBuilder& end();
//
//	std::map<std::string, CustomType> _custom_types;
//	std::map<std::string, Range> _name_range_map;
//	// std::map<std::string, Types> _name_builtin_type_map;
//	// std::map<std::string, std::string> _name_custom_type_map;
//};

struct StaticUBOAccess {
	StaticUBOAccess& operator[](const std::string& name) {
		_path.push_back({ name, 0 });
		return *this;
	}
	StaticUBOAccess& operator[](uint32_t id) {
		assert(!_path.empty());
		assert(!_path.back().field_name.empty());
		_path.back().id = id;
		return *this;
	}
	void clear() {
		_path.clear();
	}

	struct Access {
		std::string field_name;
		uint32_t id;
	};
	std::vector<Access> _path;
};

struct StaticUBO {
	StaticUBO(const Std140AlignmentType& layout);
	~StaticUBO();

	void set(StaticUBOAccess& access, void* value);

	// return offset-size
	Std140AlignmentType::Range find_range_recursive(Std140AlignmentType& type, StaticUBOAccess& access, size_t depth);


	otcv::Buffer* _buf;
	Std140AlignmentType _layout;
};