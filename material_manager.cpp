#include "material_manager.h"
#include "otcv_utils.h"
#include "tiny_gltf.h"
#include "global_handles.h"
#include "gltf_scene.h"

#include <iostream>

MaterialManager::MaterialManager(VkPhysicalDevice device, std::map<RenderPassType, std::string>& pass_shader_map) {
    ubo_manager = std::make_shared<DynamicUBOManager>(device);
    pipeline_cache = std::make_shared<PipelineCache>();
    sampler_cache = std::make_shared<SamplerCache>();
	image_cache = std::make_shared<ImageCache>();
    _texture_desc_pool = std::make_shared<SingleTypeExpandableDescriptorPool>(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    
	{
		// load shaders for grometry pass
		assert(pass_shader_map.find(RenderPassType::Geometry) != pass_shader_map.end());
		otcv::ShaderLoadHint hint;
		std::set<uint16_t> dynamic_sets = { DescriptorSetRate::PerObjectUBO, DescriptorSetRate::PerMaterialUBO };
		hint.vertex_hint = otcv::ShaderLoadHint::Hint::DynamicUBO;
		hint.vertex_custom = &dynamic_sets;
		hint.fragment_hint = otcv::ShaderLoadHint::Hint::DynamicUBO;
		hint.fragment_custom = &dynamic_sets;
		_shader_blob[RenderPassType::Geometry] = std::move(otcv::load_shaders_from_dir(pass_shader_map[RenderPassType::Geometry], hint));
	}

	{
		// load shaders for lighting pass
		assert(pass_shader_map.find(RenderPassType::Lighting) != pass_shader_map.end());
		_shader_blob[RenderPassType::Lighting] = std::move(otcv::load_shaders_from_dir(pass_shader_map[RenderPassType::Lighting]));
	}
	
}

MaterialManager::~MaterialManager() {
	for (auto& p : _shader_blob) {
		otcv::unload_shader_blob(p.second);
	}
}

bool map_sampler_config(TextureBinding::SamplerConfig config, otcv::SamplerBuilder& builder) {
	VkFilter min;
	VkFilter mag;
	// magFilter
	switch (config.mag_filter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
		mag = VK_FILTER_LINEAR;
		break;
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
		mag = VK_FILTER_NEAREST;
		break;
	default:
		std::cout << "Illegal magFilter value = " << config.mag_filter << std::endl;
		assert(false);
		return false;
		break;
	}
	// minFilter
	switch (config.min_filter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
		min = VK_FILTER_LINEAR;
		break;
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
		min = VK_FILTER_NEAREST;
		break;
	default:
		std::cout << "Illegal minFilter value = " << config.min_filter << std::endl;
		assert(false);
		return false;
		break;
	}
	builder.filter(min, mag);

	// mipmap 
	switch (config.min_filter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
		builder.mipmap(VK_SAMPLER_MIPMAP_MODE_LINEAR);
		break;
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
		builder.mipmap(VK_SAMPLER_MIPMAP_MODE_NEAREST);
		break;
	default:
		builder.mipmap(VK_SAMPLER_MIPMAP_MODE_LINEAR); // gltf did not specify mipmap. Default to linear
		break;
	}

	// wrap
	switch (config.wrap_s) {
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		builder.address_mode_u(VK_SAMPLER_ADDRESS_MODE_REPEAT);
		break;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		builder.address_mode_u(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		break;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		builder.address_mode_u(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
		break;
	default:
		std::cout << "Illegal wrapS value = " << config.wrap_s << std::endl;
		assert(false);
		break;
	}

	switch (config.wrap_t) {
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		builder.address_mode_v(VK_SAMPLER_ADDRESS_MODE_REPEAT);
		break;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		builder.address_mode_v(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		break;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		builder.address_mode_v(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
		break;
	default:
		std::cout << "Illegal wrapT value = " << config.wrap_t << std::endl;
		assert(false);
		break;
	}

	return true;
}

// pack(channels, bit_depth) | srgb, VkFormat
const std::map<uint32_t, VkFormat> format_lut = {
	{otcv::pack(4, 8) | 0x8000,	VK_FORMAT_R8G8B8A8_SRGB},
	{otcv::pack(4, 8),			VK_FORMAT_R8G8B8A8_UNORM},
	{otcv::pack(3, 8) | 0x8000,	VK_FORMAT_R8G8B8_SRGB},
	{otcv::pack(3, 8),			VK_FORMAT_R8G8B8_UNORM},
};


VkFormat choose_format(int channels, int bit_depth, bool srgb) {
	uint32_t format_index = otcv::pack((uint16_t)channels, (uint16_t)bit_depth) | (srgb ? 0x8000 : 0x0);
	auto iter = format_lut.find(format_index);
	if (iter == format_lut.end()) {
		return VK_FORMAT_UNDEFINED;
	}
	else {
		return iter->second;
	}
}

std::shared_ptr<MaterialManager::TextureResource> MaterialManager::load_texture(TextureBinding& texture, bool srgb, bool swizzle) {
	assert(texture.image);
	std::shared_ptr<TextureResource> res = std::make_shared<TextureResource>();
	// image
	ImageData& img_data = *texture.image;
	otcv::ImageBuilder imb;
	imb.size(img_data.width, img_data.height, img_data.bit_depth / 8)
		.name(img_data.uri);
	VkFormat format = choose_format(img_data.channels, img_data.bit_depth, srgb);
	assert(format != VK_FORMAT_UNDEFINED);
	imb
		.format(format)
		.usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		.enable_mips();
	if (swizzle) {
		// the reason why swizzling is necessary:
		// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#:~:text=material.pbrMetallicRoughness.metallicRoughnessTexture
		imb.swizzle(VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R);
	}
	res->image_handle = image_cache->get_handle(imb);
	otcv::Image* image = image_cache->get(res->image_handle);
	image->populate_async(img_data.pixel_data.data(), img_data.pixel_data.size(), otcv::ResourceState::FragSample);

	// sampler
	otcv::SamplerBuilder sb;
	map_sampler_config(texture.sampler_config, sb);
	res->sampler_handle = sampler_cache->get_handle(sb);

	return res;
}

MaterialHandle MaterialManager::add_material(const MaterialData& material) {
    // TODO: only non-emissive PBR at this point
    _materials.emplace_back();

	LightingModel::Model& model = (_materials.back().lighting = deduce_lighting_model(material)).model;
	LightingModel::Variant& variant = _materials.back().lighting.variant;

    // geometry pass
	{
		PipelineState& geo_pipeline_state = _materials.back().pass_action[RenderPassType::Geometry];
		// 1.pipeline
		otcv::GraphicsPipelineBuilder pipeline_builder;
		pipeline_builder.pipline_rendering()
			.add_color_attachment_format(VK_FORMAT_R8G8B8A8_SRGB)
			.add_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM)
			.add_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM)
			.depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
			.end();
		if (model == LightingModel::Model::PBR) {
			pipeline_builder
				.shader_vertex(_shader_blob[RenderPassType::Geometry]["pbr.vert"])
				.shader_fragment(_shader_blob[RenderPassType::Geometry]["pbr.frag"]);
			{
				otcv::VertexBufferBuilder vbb;
				vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2))
					.add_binding().add_attribute(3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
				pipeline_builder.vertex_state(vbb);
			}
			pipeline_builder.depth_test();
			if (!material.double_sided) {
				pipeline_builder.cull_back_face();
			}
			pipeline_builder
				.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
				.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
			geo_pipeline_state.pipeline_handle = pipeline_cache->get_handle(pipeline_builder);
		}
		else if (model == LightingModel::Model::Lambert) {
			pipeline_builder
				.shader_vertex(_shader_blob[RenderPassType::Geometry]["lambert.vert"])
				.shader_fragment(_shader_blob[RenderPassType::Geometry]["lambert.frag"]);
			{
				otcv::VertexBufferBuilder vbb;
				vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
				pipeline_builder.vertex_state(vbb);
			}
			pipeline_builder.depth_test();
			if (!material.double_sided) {
				pipeline_builder.cull_back_face();
			}
			pipeline_builder
				.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
				.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
			geo_pipeline_state.pipeline_handle = pipeline_cache->get_handle(pipeline_builder);
		}
		else {
			assert(false);
		}


		// 2.material ubos
		otcv::GraphicsPipeline* pipeline = pipeline_cache->get(geo_pipeline_state.pipeline_handle);
		if (model == LightingModel::Model::PBR) {
			//layout(set = 2, binding = 0) uniform MaterialUBO {
	//	vec4 baseColorFactor;
	//	vec4 mrnoFactor; // 4 factors: metallic - roughness - normal scale - occlusion strength
	//	uint alphaMode; // 0 - opaque, 1 - mask, 2 - blend
	//	float alphaCutoff;
	//	uint flipNormal; // 0 - dont need to flip, 1 - need to flip
	//} mUbo;
			geo_pipeline_state.material_ubo_set_handle = ubo_manager->add_descriptor_set(pipeline, DescriptorSetRate::PerMaterialUBO);
			DescriptorSetInfoHandle& handle = geo_pipeline_state.material_ubo_set_handle;
			ubo_manager->set_value(handle,
				"mUbo", "baseColorFactor",
				&material.base_color_factor, sizeof(glm::vec4));

			glm::vec4 mrno_factor(material.metallic_factor, material.roughness_factor, material.normal_scale, material.occlusion_strength);
			ubo_manager->set_value(handle,
				"mUbo", "mrnoFactor",
				&mrno_factor, sizeof(glm::vec4));

			uint32_t alpha_mode = (uint32_t)material.alpha_mode;
			ubo_manager->set_value(handle,
				"mUbo", "alphaMode",
				&alpha_mode, sizeof(uint32_t));

			ubo_manager->set_value(handle,
				"mUbo", "alphaCutoff",
				&material.alpha_cutoff, sizeof(float));

			uint32_t flip_normal = material.double_sided ? 1 : 0;
			ubo_manager->set_value(handle,
				"mUbo", "flipNormal",
				&flip_normal, sizeof(uint32_t));
		}
		else if (model == LightingModel::Model::Lambert) {
			//layout(set = 2, binding = 0) uniform ObjectUBO {
			//	vec4 baseColorFactor;
			//	uint alphaMode; // 0 - opaque, 1 - mask, 2 - blend
			//	float alphaCutoff;
			//	uint flipNormal; // 0 - dont need to flip, 1 - need to flip
			//} oUbo;
			geo_pipeline_state.material_ubo_set_handle = ubo_manager->add_descriptor_set(pipeline, DescriptorSetRate::PerMaterialUBO);
			DescriptorSetInfoHandle& handle = geo_pipeline_state.material_ubo_set_handle;
			ubo_manager->set_value(handle,
				"mUbo", "baseColorFactor",
				&material.base_color_factor, sizeof(glm::vec4));

			uint32_t alpha_mode = (uint32_t)material.alpha_mode;
			ubo_manager->set_value(handle,
				"mUbo", "alphaMode",
				&alpha_mode, sizeof(glm::vec4));

			ubo_manager->set_value(handle,
				"mUbo", "alphaCutoff",
				&material.alpha_cutoff, sizeof(float));

			uint32_t flip_normal = material.double_sided ? 1 : 0;
			ubo_manager->set_value(handle,
				"mUbo", "flipNormal",
				&flip_normal, sizeof(uint32_t));
		}
		else {
			assert(false);
		}


		// 3.textures
		geo_pipeline_state.texture_desc_set = _texture_desc_pool->allocate(pipeline->desc_set_layouts[DescriptorSetRate::PerMaterialTexture]);
		auto load_and_bind_texture = [&](uint16_t binding, TextureBinding& texture, bool srgb, bool swizzle) {
			auto res = load_texture(texture, srgb, swizzle);
			otcv::Image* img = image_cache->get(res->image_handle);
			assert(img);
			otcv::Sampler* sampler = sampler_cache->get(res->sampler_handle);
			geo_pipeline_state.texture_desc_set->bind_image_sampler(binding, &img, &sampler);
		};

		if (model == LightingModel::Model::PBR) {
			load_and_bind_texture(0, *material.base_color, true, false);
			load_and_bind_texture(1, *material.normal, false, false);
			load_and_bind_texture(2, *material.metallic_roughness, false, true);
		}
		else if (model == LightingModel::Model::Lambert) {
			load_and_bind_texture(0, *material.base_color, true, false);
		}
	}

    // lighting pass
	{
		// pipeline
		otcv::GraphicsPipelineBuilder pipeline_builder;
		pipeline_builder.pipline_rendering()
			// TODO: should consider HDR rendering
			.add_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM)
			/// TODO: depth test to accomodate mock depth attachment
			.depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
			.end();
		pipeline_builder
			.shader_vertex(_shader_blob[RenderPassType::Lighting]["screen_quad.vert"]);
		{
			otcv::VertexBufferBuilder vbb;
			vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
				.add_binding().add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
			pipeline_builder.vertex_state(vbb);
		}
		pipeline_builder
			.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);

		/// TODO: depth test to accomodate mock depth attachment
		// pipeline_builder.depth_test();

		if (model == LightingModel::Model::PBR) {
			pipeline_builder.shader_fragment(_shader_blob[RenderPassType::Lighting]["pbr.frag"]);
		}
		else if (model == LightingModel::Model::Lambert) {
			pipeline_builder.shader_fragment(_shader_blob[RenderPassType::Lighting]["lambert.frag"]);
		}
		else {
			assert(false);
		}
		_materials.back().pass_action[RenderPassType::Lighting].pipeline_handle = pipeline_cache->get_handle(pipeline_builder);
	}

	return MaterialHandle{ _materials.size() - 1 };
}

bool MaterialManager::has_pass(MaterialHandle handle, RenderPassType pass) {
	assert(handle.id < _materials.size());
	return _materials[handle.id].pass_action.find(pass) != _materials[handle.id].pass_action.end();
}

MaterialManager::PipelineState MaterialManager::get_pipeline_state(MaterialHandle handle, RenderPassType pass) {
	assert(handle.id < _materials.size());
	assert(_materials[handle.id].pass_action.find(pass) != _materials[handle.id].pass_action.end());
	return _materials[handle.id].pass_action[pass];
}

MaterialManager::Material MaterialManager::get_material(MaterialHandle handle) {
	assert(handle.id < _materials.size());
	return _materials[handle.id];
}

otcv::DescriptorSetLayout* MaterialManager::per_frame_desc_set_layout(RenderPassType pass) {
	// grab any material that gets drawn at this specific pass
	// presumably all materials that get drawn at this pass should have the same per frame descriptor set layout
	otcv::GraphicsPipeline* pipeline = nullptr;
	for (Material& m : _materials) {
		auto iter = m.pass_action.find(pass);
		if (iter == m.pass_action.end()) {
			assert(false);
			return nullptr;
		}
		pipeline = pipeline_cache->get(iter->second.pipeline_handle);
		assert(pipeline);
		break;
	}
	return pipeline->desc_set_layouts[DescriptorSetRate::PerFrameUBO];
}

LightingModel MaterialManager::deduce_lighting_model(const MaterialData& material) {
	// TODO: temp
	LightingModel lighting;
	if (material.base_color && material.normal && material.metallic_roughness) {
		lighting.model = LightingModel::Model::PBR; // TODO: differentiate between emissive and non-emissive
	}
	else if (material.base_color) {
		lighting.model = LightingModel::Model::Lambert;
	}
	else {
		lighting.model = LightingModel::Model::Unlit;
	}

	return lighting;
}