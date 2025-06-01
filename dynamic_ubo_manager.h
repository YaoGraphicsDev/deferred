#pragma once

#include "otcv.h"
#include "expandable_descriptor_pool.h"
#include "shared_object_cache.h"
#include "global_handles.h"

class DynamicUBOManager {
public:
	struct DescriptorSetCacheKey {
		DescriptorSetCacheKey(
			std::vector<std::string>& ubo_names,
			std::vector<std::map<std::string, VkDeviceSize>>& ubo_fields,
			std::vector<VkShaderStageFlags> ubo_stages
		);

		// sizes of each binding
		// DescriptorSetCacheKey(const std::vector<VkDeviceSize>& sizes) : _sizes(sizes) {};
		DescriptorSetCacheKey() {};
		std::vector<uint8_t> serialized;
		bool operator==(const DescriptorSetCacheKey& other) const {
			return serialized == other.serialized;
		}
	};

	struct DescriptorSetCacheKeyHash {
		std::size_t operator()(const DescriptorSetCacheKey& handle) const {
			SequenceHash<std::vector<uint8_t>> hasher;
			return hasher(handle.serialized);
		}
	};

	struct DescriptorSetInfo {
		DescriptorSetCacheKey key;
		struct Access {
			VkDeviceSize offset; // offset in buffer
			VkDeviceSize size;
		};
		// binding -- access
		std::vector<Access> ubo_accesses;
	};

	DynamicUBOManager(VkPhysicalDevice physicalDevice);
	~DynamicUBOManager();

	// return a handle
	DescriptorSetInfoHandle add_descriptor_set(otcv::GraphicsPipeline* pipeline, uint16_t set);
	void copy_to_ubo(DescriptorSetInfoHandle handle, uint16_t binding, void* data, uint32_t size);
	void set_value(DescriptorSetInfoHandle handle, const std::string& ubo_name, const std::string& field_name, const void* data, uint32_t size);
	DescriptorSetInfo get_descriptor_set_info(DescriptorSetInfoHandle handle);

	std::unordered_map<DescriptorSetCacheKey, otcv::DescriptorSet*, DescriptorSetCacheKeyHash> desc_set_cache;

private:
	// return value: offset in buffer -- buffer is reallocated
	std::pair<VkDeviceSize, bool> claim_buffer_chunk(VkDeviceSize size);
	void rebind_all();
	otcv::DescriptorSet* allocate_desc_set(otcv::DescriptorSetLayout* layout);


	std::shared_ptr<SingleTypeExpandableDescriptorPool> _desc_pool;

	otcv::Buffer* _ubo_buffer;
	VkDeviceSize _alignment;
	VkDeviceSize _ubo_size_in_use;
	const VkDeviceSize _buffer_init_size = 256;

	std::vector<DescriptorSetInfo> _desc_infos;

	struct BindingFieldInfo {
		uint16_t binding;
		std::map<std::string, VkDeviceSize> field_offset;
	};
	//  descriptor set key -- ubo name (one particular binding) -- field info
	std::unordered_map<DescriptorSetCacheKey, std::map<std::string, BindingFieldInfo>, DescriptorSetCacheKeyHash> _set_field_maps;
};