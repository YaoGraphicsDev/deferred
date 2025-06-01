#pragma once

#include "otcv.h"

#include <cassert>

struct SingleTypeExpandableDescriptorPool {
	SingleTypeExpandableDescriptorPool(VkDescriptorType type) {
		_desc_type = type;
		_desc_record = 0;
		_set_record = 0;
		_desc_available = 8;
		_set_available = 2;
		otcv::DescriptorPoolBuilder builder;
		builder.descriptor_set_capacity(_set_available).descriptor_type_capacity(type, _desc_available);
		_pools.push_back(new otcv::DescriptorPool(builder));
		_expansion_track.push_back({ _set_available, _desc_available });
	};
	~SingleTypeExpandableDescriptorPool() {
		for (auto& p : _pools) {
			delete p;
		}
		_pools.clear();
	}

	otcv::DescriptorSet* allocate(otcv::DescriptorSetLayout* layout) {
		// assume there are not many bindings in a set
		uint32_t desc_required = 0;
		for (auto& binding : layout->bindings) {
			if (binding.descriptorType != _desc_type) {
				return nullptr;
			}
			desc_required += binding.descriptorCount;
		}

		if (_set_available == 0 || _desc_available - desc_required < 0) {
			uint32_t desc_expansion = _desc_record > desc_required ? _desc_record : desc_required;
			assert(_set_record != 0);
			float ratio = float(_desc_record) / float(_set_record);
			uint32_t set_expansion = std::max(std::ceil(desc_expansion / ratio), 1.0f);

			otcv::DescriptorPoolBuilder builder;
			builder
				.descriptor_set_capacity(set_expansion)
				.descriptor_type_capacity(_desc_type, desc_expansion);
			_pools.push_back(new otcv::DescriptorPool(builder));
			_expansion_track.push_back({ set_expansion, desc_expansion });
			_set_available = set_expansion;
			_desc_available = desc_expansion;
		}

		otcv::DescriptorSet* set = _pools.back()->allocate(layout, []() {assert(false); }); // should never oom
		--_set_available;
		_desc_available -= desc_required;
		_set_record++;
		_desc_record += desc_required;
		return set;
	}

	VkDescriptorType _desc_type;
	int _desc_record;
	int _set_record;
	int _desc_available;
	int _set_available;
	std::vector<otcv::DescriptorPool*> _pools;

	// debug only. set -- desc
	std::vector<std::pair<int, int>> _expansion_track;
};

struct TrivialExpandableDescriptorPool {
	TrivialExpandableDescriptorPool() {};
	~TrivialExpandableDescriptorPool() {
		for (auto& p : _pools) {
			delete p;
		}
		_pools.clear();
	}

	otcv::DescriptorSet* allocate(otcv::DescriptorSetLayout* layout) {
		if (layout->bindings.empty()) {
			return nullptr;
		}

		std::map<uint32_t, uint32_t> desc_required;
		for (auto& binding : layout->bindings) {
			uint32_t type = (uint32_t)binding.descriptorType;
			uint32_t& required = desc_required[type];
			required += binding.descriptorCount;
		}

		otcv::DescriptorPoolBuilder builder;
		builder.descriptor_set_capacity(1);
		for (auto& p : desc_required) {
			uint32_t type = p.first;
			uint32_t capacity = p.second;
			builder.descriptor_type_capacity((VkDescriptorType)type, capacity);
		}
		_pools.push_back(new otcv::DescriptorPool(builder));

		otcv::DescriptorSet* set = _pools.back()->allocate(layout, []() {assert(false); }); // should never oom
		return set;
	}

	std::vector<otcv::DescriptorPool*> _pools;
};