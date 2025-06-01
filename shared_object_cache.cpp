#include "shared_object_cache.h"

#include <cassert>

void serialize_string(std::vector<uint8_t>& serialized, const std::string& str) {
    uint8_t* start = (uint8_t*)(str.data());
    size_t length = str.length();
    serialized.insert(serialized.end(), start, start + length);
}

PipelineHandle::PipelineHandle(otcv::GraphicsPipelineBuilder& gpb) {
	// collect all hashable fields and concatenate them into a vector
    uint8_t* start = nullptr;
    size_t length = 0;

    serialize_string(serialized, gpb._vertex_shader->builder._name);
    serialize_string(serialized, gpb._fragment_shader->builder._name);
    serialize_trivial(serialized, gpb._rast_state.cullMode);
    serialize_trivial(serialized, gpb._rast_state.frontFace);

    // TODO: include other distinguishable fields later
}

SamplerHandle::SamplerHandle(otcv::SamplerBuilder& sb) {
    // collect all hashable fields and concatenate them into a vector
    uint8_t* start = (uint8_t*)(&sb._info.flags);
    size_t length = sizeof(VkSamplerCreateInfo) - offsetof(VkSamplerCreateInfo, flags);

    serialized.insert(serialized.end(), start, start + length);
}

ImageByNameHandle::ImageByNameHandle(otcv::ImageBuilder& sb) {
    assert(!sb._name.empty());
    this->name = sb._name;
}