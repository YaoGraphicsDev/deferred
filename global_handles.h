#pragma once

struct AttributeHandle {
	size_t id;
};

struct MaterialHandle {
	size_t id;
	bool operator<(const MaterialHandle& other) const {
		return this->id < other.id;
	}
	bool operator==(const MaterialHandle& other) const {
		return this->id == other.id;
	}
};

struct TextureHandle {
	size_t id;
	size_t offset;
	size_t length;
};

struct DescriptorSetInfoHandle {
	size_t id;
};

struct PipelineHandle {
	std::vector<uint8_t> serialized;
	PipelineHandle(otcv::GraphicsPipelineBuilder& sb);
	PipelineHandle() {};
	bool operator==(const PipelineHandle& other) const {
		return this->serialized == other.serialized;
	}
	bool operator<(const PipelineHandle& other) const {
		return this->serialized < other.serialized;
	}
};

struct SamplerHandle {
	std::vector<uint8_t> serialized;
	SamplerHandle(otcv::SamplerBuilder& sb);
	SamplerHandle() {}
	bool operator==(const SamplerHandle& other) const {
		return this->serialized == other.serialized;
	}
};

struct ImageByNameHandle {
	std::string name;
	ImageByNameHandle(otcv::ImageBuilder& sb);
	ImageByNameHandle() {};
	bool operator==(const ImageByNameHandle& other) const {
		return this->name == other.name;
	}
};