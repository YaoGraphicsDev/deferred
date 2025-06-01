#pragma once

#include "otcv.h"
#include "global_handles.h"

#include <map>
#include <vector>

template <typename _Type, typename _Builder, typename _Key>
class GrowingCache {
public:
	GrowingCache() {}
	~GrowingCache() {
		for (auto p : _cache) {
			delete p.second;
		}
		_cache.clear();
	}

	_Key get_handle(_Builder& gpb) {
		_Key key(gpb);
		auto iter = _cache.find(key);
		if (iter == _cache.end()) {
			_cache[key] = new _Type(gpb);
		}
		return key;
	}

	_Type* get(const _Key& key) {
		auto iter = _cache.find(key);
		if (iter == _cache.end()) {
			return nullptr;
		}
		else {
			return iter->second;
		}
	}

	typename std::unordered_map<_Key, _Type*>::iterator begin() {
		return _cache.begin();
	}
	
	typename std::unordered_map<_Key, _Type*>::iterator end() {
		return _cache.end();
	}

private:
	std::unordered_map<_Key, _Type*> _cache;
};

typedef GrowingCache<otcv::Sampler, otcv::SamplerBuilder, SamplerHandle> SamplerCache;

typedef GrowingCache<otcv::GraphicsPipeline, otcv::GraphicsPipelineBuilder, PipelineHandle> PipelineCache;

typedef GrowingCache<otcv::Image, otcv::ImageBuilder, ImageByNameHandle> ImageCache;

template <typename Con>
struct SequenceHash {
	size_t operator()(const Con& container) const {
		size_t hash = 0;
		std::hash<Con::value_type> hasher;
		for (const Con::value_type& item : container) {
			// A common hash combining strategy (boost-like)
			hash ^= hasher(item) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

namespace std {
	template <>
	struct hash<SamplerHandle> {
		std::size_t operator()(const SamplerHandle& handle) const {
			SequenceHash<std::vector<uint8_t>> hasher;
			return hasher(handle.serialized);
		}
	};

	template <>
	struct hash<PipelineHandle> {
		std::size_t operator()(const PipelineHandle& handle) const {
			SequenceHash<std::vector<uint8_t>> hasher;
			return hasher(handle.serialized);
		}
	};

	template <>
	struct hash<ImageByNameHandle> {
		std::size_t operator()(const ImageByNameHandle& handle) const {
			SequenceHash<std::string> hasher;
			return hasher(handle.name);
		}
	};
}

template<typename T>
void serialize_trivial(std::vector<uint8_t>& serialized, T value) {
	uint8_t* start = (uint8_t*)(&value);
	size_t length = sizeof(value);
	serialized.insert(serialized.end(), start, start + length);
}

void serialize_string(std::vector<uint8_t>& serialized, const std::string& str);
