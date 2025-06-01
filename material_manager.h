#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "dynamic_ubo_manager.h"
#include "shared_object_cache.h"
#include "render_global_types.h"
#include "gltf_scene.h"

class MaterialManager {
public:
	struct PipelineState {
		PipelineHandle pipeline_handle;
		DescriptorSetInfoHandle material_ubo_set_handle;
		otcv::DescriptorSet* texture_desc_set;
	};
	typedef std::map<RenderPassType, PipelineState> PassAction;
	struct Material {
		LightingModel lighting;
		PassAction pass_action;
	};

	MaterialManager(VkPhysicalDevice device,
		std::map<RenderPassType, std::string>& pass_shader_map);
	~MaterialManager();

	MaterialHandle add_material(const MaterialData& material);
	
	// Check if a material has anything to do with a certain pass
	bool has_pass(MaterialHandle handle, RenderPassType pass);

	PipelineState get_pipeline_state(MaterialHandle handle, RenderPassType pass);

	Material get_material(MaterialHandle handle);

	otcv::DescriptorSetLayout* per_frame_desc_set_layout(RenderPassType pass);
	
	std::shared_ptr<DynamicUBOManager> ubo_manager;
	std::shared_ptr<PipelineCache> pipeline_cache;
	std::shared_ptr<SamplerCache> sampler_cache;
	std::shared_ptr<ImageCache> image_cache;

private:

	// TODO: material deduction needs to know what attributes the mesh offer
	LightingModel deduce_lighting_model(const MaterialData& material);
	//bool build_pipeline(LightingModel model, otcv::GraphicsPipelineBuilder& pb);
	//bool build_ubos(LightingMode model, );

	struct TextureResource {
		ImageByNameHandle image_handle;
		SamplerHandle sampler_handle;
	};
	std::shared_ptr<TextureResource> load_texture(TextureBinding& texture, bool srgb, bool swizzle);

	std::map<RenderPassType, otcv::ShaderBlob> _shader_blob;

	std::shared_ptr<SingleTypeExpandableDescriptorPool> _texture_desc_pool;

	std::vector<Material> _materials;
};