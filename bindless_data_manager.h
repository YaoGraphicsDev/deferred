#pragma once
#include "otcv.h"
#include "otcv_utils.h"
#include "gltf_scene_bindless.h"
#include "static_ubo.h"
#include "expandable_descriptor_pool.h"

#include <map>
#include <vector>
#include <glm/glm.hpp>

class BindlessDataManager {
public:
	BindlessDataManager(VkPhysicalDevice physical_device,
		const std::string& shader_path,
		uint32_t n_objects,
		uint32_t n_materials,
		uint32_t n_images,
		uint32_t n_samplers);
	~BindlessDataManager();

	struct AttributeHandle {
		bool valid = false;
		size_t offset = 0;
		size_t length = 0;
	};

	struct MeshHandle {
		size_t id = 0;
		AttributeHandle position;
		AttributeHandle normal;
		AttributeHandle uv0;
		AttributeHandle tangent;
	};

	bool set_materials(const MaterialResources& mat_res);

	void set_objects(const SceneGraph& graph, const SceneGraphFlatRefs& graph_refs);

	struct ObjectDataSegment {
		uint32_t index_start;
		uint32_t index_count;
		int vertex_start;
	};
	
	otcv::DescriptorSetLayout* frame_descriptor_set_layout() {
		return _pipeline_bins.begin()->second->desc_set_layouts[DescriptorSetRate::PerFrame];
	}

	uint32_t _ubo_alignment;
	uint32_t _n_objects;
	uint32_t _n_materials;
	uint32_t _n_images;
	uint32_t _n_samplers;

	otcv::VertexBuffer* _vb;
	otcv::Buffer* _ib;

	std::vector<ObjectDataSegment> _object_data_segment;

	std::shared_ptr<StaticUBOArray> _object_ubos;
	std::shared_ptr<StaticUBOArray> _material_ubos;

	std::vector<otcv::Image*> _images;
	std::vector<otcv::Sampler*> _samplers;

	std::shared_ptr<NaiveExpandableDescriptorPool> _bindless_desc_pool;
	otcv::DescriptorSet* _bindless_object_desc_set;
	otcv::DescriptorSet* _bindless_material_desc_set;

	std::map<PipelineVariant, otcv::GraphicsPipeline*> _pipeline_bins;
	otcv::ShaderBlob _shader_blob;
	
private:

	void build_all_pipelines(const std::string& shader_path);

	void build_descriptor_sets();

	VkFormat choose_format(int channels, int bit_depth, bool srgb);

	otcv::Image* upload_image_async(ImageData& img_data, bool srgb, bool swizzle);

	// pack(channels, bit_depth) | color_space, VkFormat
	const std::map<uint32_t, VkFormat> format_lut = {
	{otcv::pack(4, 8) | 0x8000,	VK_FORMAT_R8G8B8A8_SRGB},
	{otcv::pack(4, 8),			VK_FORMAT_R8G8B8A8_UNORM},
	{otcv::pack(3, 8) | 0x8000,	VK_FORMAT_R8G8B8_SRGB},
	{otcv::pack(3, 8),			VK_FORMAT_R8G8B8_UNORM},
	};

};
