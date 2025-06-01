#include "dynamic_ubo_manager.h"
#include "otcv_utils.h"

#include <cassert>
#include <iostream>


DynamicUBOManager::DynamicUBOManager(VkPhysicalDevice physicalDevice) {
	otcv::BufferBuilder bb;
	bb.size(_buffer_init_size)
		.usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
		.host_access(otcv::BufferBuilder::Access::Coherent);
	_ubo_buffer = new otcv::Buffer(bb); // owned by this class. Not under global management

	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(physicalDevice, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	_alignment = limits.minUniformBufferOffsetAlignment;

	_ubo_size_in_use = 0;

	_desc_pool = std::make_shared<SingleTypeExpandableDescriptorPool>(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
}

DynamicUBOManager::~DynamicUBOManager() {
	delete _ubo_buffer;
	_desc_infos.clear();
}

DynamicUBOManager::DescriptorSetCacheKey::DescriptorSetCacheKey(
	std::vector<std::string>& ubo_names,
	std::vector<std::map<std::string, VkDeviceSize>>& ubo_fields,
	std::vector<VkShaderStageFlags> ubo_stages) {
    for (std::string& str : ubo_names) {
        serialize_string(serialized, str);
    }
    for (std::map<std::string, VkDeviceSize>& m : ubo_fields) {
        for (std::pair<const std::string, VkDeviceSize>& p : m) {
            serialize_string(serialized, p.first);
            serialize_trivial(serialized, p.second);
        }
    }
	for (VkShaderStageFlags& flag : ubo_stages) {
		serialize_trivial(serialized, flag);
	}
}

std::pair<VkDeviceSize, bool> DynamicUBOManager::claim_buffer_chunk(VkDeviceSize size) {
	assert(size % _alignment == 0); // check alignment
	otcv::BufferBuilder bb = _ubo_buffer->builder;
	VkDeviceSize current_offset = _ubo_size_in_use;
	_ubo_size_in_use += size;
	if (_ubo_size_in_use <= bb._info.size) {
		// enough space
		return { current_offset, false };
	}
	while (bb._info.size < _ubo_size_in_use) {
		bb.size(bb._info.size * 2);
	}
	otcv::Buffer* new_buffer = new otcv::Buffer(bb);
	assert(bb._mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	assert(new_buffer->mapped && _ubo_buffer->mapped);
	new_buffer->copy_host_mapped(_ubo_buffer->mapped, 0, _ubo_buffer->builder._info.size);
	delete _ubo_buffer;
	_ubo_buffer = new_buffer;
	return { current_offset, true };
}

otcv::DescriptorSet* DynamicUBOManager::allocate_desc_set(otcv::DescriptorSetLayout* layout) {
	return _desc_pool->allocate(layout);
}


void account(
	otcv::GraphicsPipeline* pipeline,
	uint16_t set,
	VkDeviceSize alignment,
	std::vector<VkDeviceSize>& ubo_sizes,
	std::vector<std::string>& ubo_names,
	std::vector<std::map<std::string, VkDeviceSize>>& ubo_fields,
	std::vector<VkShaderStageFlags> ubo_stages) {

	size_t bindings_count = pipeline->desc_set_layouts[set]->bindings.size();
	ubo_sizes.resize(bindings_count);
	ubo_names.resize(bindings_count);
	ubo_fields.resize(bindings_count);
	ubo_stages.resize(bindings_count);

	size_t ubo_size;
	for (uint16_t binding = 0; binding < pipeline->desc_set_layouts[set]->bindings.size(); ++binding) {
		uint32_t key = otcv::pack(set, binding);

		auto& uniform = otcv::uniform_at(pipeline, set, binding);
		// Do not allow any descriptor type other than dynamic uniform buffer
		assert(uniform._type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		if (uniform._type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
			ubo_sizes[binding] = (uniform._size + alignment - 1) & ~(alignment - 1);
			assert(ubo_sizes[binding] % alignment == 0);

			ubo_names[binding] = uniform._name;
			ubo_fields[binding] = uniform._field_offset_map;
			ubo_stages[binding] = pipeline->desc_set_layouts[set]->bindings[binding].stageFlags;
		}
	}
}

void DynamicUBOManager::rebind_all() {
	for (DescriptorSetInfo& info : _desc_infos) {
		otcv::DescriptorSet* set = desc_set_cache[info.key];
		for (uint32_t binding = 0; binding < info.ubo_accesses.size(); ++binding) {
			set->bind_buffer(binding, _ubo_buffer, 0, info.ubo_accesses[binding].size);
		}
	}
}

DescriptorSetInfoHandle DynamicUBOManager::add_descriptor_set(otcv::GraphicsPipeline* pipeline, uint16_t set) {
	_desc_infos.emplace_back();
	DescriptorSetInfoHandle handle{ _desc_infos.size() - 1 };
	DescriptorSetInfo& desc_info = _desc_infos.back();

	std::vector<VkDeviceSize> ubo_sizes;
	std::vector<std::string> ubo_names;
	std::vector<std::map<std::string, VkDeviceSize>> ubo_fields;
	std::vector<VkShaderStageFlags> ubo_stages;
	account(pipeline, set, _alignment, ubo_sizes, ubo_names, ubo_fields, ubo_stages);

	// check if a suitable descriptor set exists
	DescriptorSetCacheKey cache_key(ubo_names, ubo_fields, ubo_stages);
	otcv::DescriptorSet* current_set = nullptr;
	auto cache_iter = desc_set_cache.find(cache_key);
	if (cache_iter == desc_set_cache.end()) {
		// No same type of descriptor set available
		current_set = (desc_set_cache[cache_key] = _desc_pool->allocate(pipeline->desc_set_layouts[set]));
	}
	else {
		current_set = cache_iter->second;
	}
	desc_info.key = cache_key;

	bool reallocated = false;
	for (VkDeviceSize& size : ubo_sizes) {
		auto result	= claim_buffer_chunk(size);
		VkDeviceSize offset = result.first;
		desc_info.ubo_accesses.push_back({ offset, size });
		reallocated |= result.second;
	}

	if (reallocated) {
		rebind_all();
	}
	else {
		for (uint32_t binding = 0; binding < ubo_sizes.size(); ++binding) {
			current_set->bind_buffer(binding, _ubo_buffer, 0, ubo_sizes[binding]);
		}
	}

	auto field_iter = _set_field_maps.find(cache_key);
	if (field_iter == _set_field_maps.end()) {
		std::map<std::string, BindingFieldInfo>& fields = _set_field_maps[cache_key];
		for (uint32_t binding = 0; binding < ubo_sizes.size(); ++binding) {
			fields[ubo_names[binding]] = BindingFieldInfo{ (uint16_t)binding, ubo_fields[binding] };
		}
	}

	return handle;
}

void DynamicUBOManager::copy_to_ubo(DescriptorSetInfoHandle handle, uint16_t binding, void* data, uint32_t size) {
	assert(handle.id < _desc_infos.size());
	assert(binding < _desc_infos[handle.id].ubo_accesses.size());
	assert(size == _desc_infos[handle.id].ubo_accesses[binding].size); // failure indicates potential alignment problem
	_ubo_buffer->copy_host_mapped(
		data,
		_desc_infos[handle.id].ubo_accesses[binding].offset,
		_desc_infos[handle.id].ubo_accesses[binding].size);
}

void DynamicUBOManager::set_value(DescriptorSetInfoHandle handle, const std::string& ubo_name, const std::string& field_name,
	const void* data, uint32_t size) {
	assert(handle.id < _desc_infos.size());
	
	auto& set_fields = _set_field_maps[_desc_infos[handle.id].key];

	assert(set_fields.find(ubo_name) != set_fields.end());
	assert(set_fields[ubo_name].field_offset.find(field_name) != set_fields[ubo_name].field_offset.end());

	uint16_t binding = set_fields[ubo_name].binding;
	VkDeviceSize ubo_offset = _desc_infos[handle.id].ubo_accesses[binding].offset;
	VkDeviceSize field_offset = set_fields[ubo_name].field_offset[field_name];
	VkDeviceSize total_offset = ubo_offset + field_offset;
	_ubo_buffer->copy_host_mapped(data, total_offset, size);
}

DynamicUBOManager::DescriptorSetInfo DynamicUBOManager::get_descriptor_set_info(DescriptorSetInfoHandle handle) {
	assert(handle.id < _desc_infos.size());
	return _desc_infos[handle.id];
}