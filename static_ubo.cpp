#include "static_ubo.h"
#include <cassert>

uint32_t get_base_alignment(Std140AlignmentType::InlineType type, bool in_array) {
    switch (type) {
    case Std140AlignmentType::InlineType::Float:
    case Std140AlignmentType::InlineType::Uint:
        return in_array ? 16 : 4;
    case Std140AlignmentType::InlineType::Vec2:
        return in_array ? 16 : 8;
    case Std140AlignmentType::InlineType::Vec3:
    case Std140AlignmentType::InlineType::Vec4:
    case Std140AlignmentType::InlineType::Mat4:
        return 16;
    default:
        assert(false);
        return in_array ? 16 : 4;
    }
}

uint32_t get_size(Std140AlignmentType::InlineType type, bool in_array) {
    switch (type) {
    case Std140AlignmentType::InlineType::Float:
    case Std140AlignmentType::InlineType::Uint:
        return in_array ? 16 : 4;
    case Std140AlignmentType::InlineType::Vec2:
        return in_array ? 16 : 8;
    case Std140AlignmentType::InlineType::Vec3:
        return in_array ? 16 : 12;
    case Std140AlignmentType::InlineType::Vec4:
        return 16;
    case Std140AlignmentType::InlineType::Mat4:
        return 64;
    default:
        assert(false);
        return in_array ? 16 : 4;
    }
}

uint32_t round_up_to(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

Std140AlignmentType& Std140AlignmentType::add(InlineType type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = get_base_alignment(type, array_count > 1);

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = get_size(type, array_count > 1);
    range.size = range.stride * array_count;
    
    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_inline_type_map[name] = type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

Std140AlignmentType& Std140AlignmentType::add(Std140AlignmentType& custom_type, const std::string& name, uint32_t array_count) {
    uint32_t base_alignment = 16;

    Range range;
    range.offset = round_up_to(_total_size, base_alignment);
    range.stride = round_up_to(custom_type._total_size, 16);
    range.size = range.stride * array_count;

    _field_ranges.push_back(range);
    _field_name_range_map[name] = _field_ranges.size() - 1;
    _field_name_custom_type_map[name] = custom_type;
    _total_size = range.offset + range.size; // ignore trailing padding
    return *this;
}

Std140AlignmentType::Range StaticUBO::find_range_recursive(Std140AlignmentType& type, StaticUBOAccess& access, size_t depth) {
    StaticUBOAccess::Access& acc = access._path[depth];
    assert(type._field_name_range_map.find(acc.field_name) != type._field_name_range_map.end());
    Std140AlignmentType::Range& range = type._field_ranges[type._field_name_range_map[acc.field_name]];

    auto& inline_type_iter = type._field_name_inline_type_map.find(acc.field_name);
    if (inline_type_iter != type._field_name_inline_type_map.end()) {
        // inline type found
        Std140AlignmentType::InlineType type = inline_type_iter->second;
        Std140AlignmentType::Range result;
        VkDeviceSize offset = range.offset + acc.id * range.stride;
        VkDeviceSize size = get_size(type, false);
        return { offset, 0, size };
    }

    auto& custom_type_iter = type._field_name_custom_type_map.find(acc.field_name);
    if (custom_type_iter != type._field_name_custom_type_map.end()) {
        // custom type found
        Std140AlignmentType& type = custom_type_iter->second;
        Std140AlignmentType::Range& sub_range = find_range_recursive(type, access, ++depth);
        VkDeviceSize offset = range.offset + acc.id * range.stride + sub_range.offset;
        VkDeviceSize size = sub_range.size;
        return { offset, 0, size };
    }

    // acc.field_name does not exist
    assert(false);
    return {0, 0, 0};
}

StaticUBO::StaticUBO(const Std140AlignmentType& layout) {
    otcv::BufferBuilder bb;
    bb.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .host_access(otcv::BufferBuilder::Access::Coherent)
        .size(layout._total_size);
    _buf = new otcv::Buffer(bb);
    _layout = layout;
}

StaticUBO::~StaticUBO() {
    delete _buf;
    _buf = nullptr;
}

void StaticUBO::set(StaticUBOAccess& access, const void* value) {
    Std140AlignmentType::Range range = find_range_recursive(_layout, access, 0);

    assert(_buf->mapped);
    std::memcpy((char*)_buf->mapped + range.offset, value, range.size);
}
