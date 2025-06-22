#pragma once
#include "bindless_data_manager.h"
#include "otcv_utils.h"
#include "tiny_gltf.h"

#include <iostream>

BindlessDataManager::BindlessDataManager(VkPhysicalDevice physical_device,
	const std::string& shader_path,
	uint32_t n_objects,
	uint32_t n_materials,
	uint32_t n_images,
	uint32_t n_samplers) {

	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	_ubo_alignment = limits.minUniformBufferOffsetAlignment;
	if (limits.maxPerStageDescriptorSampledImages < n_images) {
		assert(false);
		std::cout << "number of images = " << n_images <<
			" exceeds maxPerStageDescriptorSampledImages = " << limits.maxPerStageDescriptorSampledImages << std::endl;
		exit(1);
	}
	if (limits.maxPerStageDescriptorSamplers < n_samplers) {
		assert(false);
		std::cout << "number of samplers = " << n_samplers <<
			" exceeds maxPerStageDescriptorSamplers = " << limits.maxPerStageDescriptorSamplers << std::endl;
		exit(1);
	}
	
	_n_objects = n_objects;
	_n_materials = n_materials;
	_n_images = n_images;
	_n_samplers = n_samplers;

	build_all_pipelines(shader_path);
	build_descriptor_sets();
}

BindlessDataManager::~BindlessDataManager() {
	delete _vb;
	delete _ib;
	for (otcv::Image* img : _images) {
		delete img;
	}
	for (otcv::Sampler* sampler : _samplers) {
		delete sampler;
	}
	for (auto& p : _pipeline_bins) {
		delete p.second;
	}
}

void BindlessDataManager::build_all_pipelines(const std::string& shader_path) {
	{
		std::map<uint32_t, uint32_t> vs_indexing_limits = {
			{otcv::pack(DescriptorSetRate::PerObject, 0), _n_objects}
		};
		std::map<uint32_t, uint32_t> fs_indexing_limits = {
			{otcv::pack(DescriptorSetRate::PerMaterial, 0), _n_materials},
			{otcv::pack(DescriptorSetRate::PerMaterial, 1), _n_images},
			{otcv::pack(DescriptorSetRate::PerMaterial, 2), _n_samplers}
		};
		std::map<std::string, otcv::ShaderLoadHint> file_hints = {
			{"geometry.vert", {otcv::ShaderLoadHint::Hint::DescriptorIndexing, &vs_indexing_limits}},
			{"geometry.frag", {otcv::ShaderLoadHint::Hint::DescriptorIndexing, &fs_indexing_limits}}
		};
		_shader_blob = otcv::load_shaders_from_dir(shader_path, file_hints);

		// geometry pass, culled
		{
			otcv::GraphicsPipelineBuilder builder;
			builder.pipline_rendering()
				.add_color_attachment_format(VK_FORMAT_R8G8B8A8_SRGB)
				.add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
				.add_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM)
				.depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
				.end();
			builder
				.shader_vertex(_shader_blob["geometry.vert"])
				.shader_fragment(_shader_blob["geometry.frag"]);
			otcv::VertexBufferBuilder vbb;
			vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
				.add_binding().add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
				.add_binding().add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2))
				.add_binding().add_attribute(3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
			builder.vertex_state(vbb);
			builder.depth_test().cull_back_face();
			builder
				.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
				.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
			_pipeline_bins[PipelineVariant::BackFaceCulled] = new otcv::GraphicsPipeline(builder);
		}

		// geometry pass, double-sided
		{
			otcv::GraphicsPipelineBuilder builder;
			builder.pipline_rendering()
				.add_color_attachment_format(VK_FORMAT_R8G8B8A8_SRGB)
				.add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
				.add_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM)
				.depth_stencil_attachment_format(VK_FORMAT_D24_UNORM_S8_UINT)
				.end();
			builder
				.shader_vertex(_shader_blob["geometry.vert"])
				.shader_fragment(_shader_blob["geometry.frag"]);
			{
				otcv::VertexBufferBuilder vbb;
				vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
					.add_binding().add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2))
					.add_binding().add_attribute(3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
				builder.vertex_state(vbb);
			}
			builder.depth_test().cull_back_face();
			builder
				.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
				.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
			_pipeline_bins[PipelineVariant::DoubleSided] = new otcv::GraphicsPipeline(builder);
		}

		// lighting pass, pbr
		// {
		// 	otcv::GraphicsPipelineBuilder builder;
		// 	builder.pipline_rendering()
		// 		.add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT) // HDR
		// 		.end();
		// 	builder
		// 		.shader_vertex(_shader_blob["screen_quad.vert"])
		// 		.shader_fragment(_shader_blob["pbr.frag"]);
		// 	otcv::VertexBufferBuilder vbb;
		// 	vbb.add_binding()
		// 		.add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
		// 		.add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
		// 	builder.vertex_state(vbb);
		// 
		// 	builder
		// 		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		// 		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
		// 
		// 	/// TODO: depth test to accomodate mock depth attachment
		// 	// pipeline_builder.depth_test();
		// 
		// 	_pipeline_bins[RenderPassType::Lighting][PipelineVariant::PBR] = new otcv::GraphicsPipeline(builder);
		// }
	}
}


void BindlessDataManager::build_descriptor_sets() {
	// descriptor pool and descriptors
	{
		otcv::DescriptorPoolBuilder pool_builder;
		pool_builder.bindless()
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _n_images)
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_SAMPLER, _n_samplers)
			.descriptor_type_capacity(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _n_materials + _n_objects)
			.descriptor_set_capacity(1);
		_bindless_desc_pool.reset(new NaiveExpandableDescriptorPool);

		// TODO: allocate descriptor sets
		auto oom_cb = []() {
			assert(false);
		};
		// grab any variant of geometry pipeline. descriptor set layouts should be identical
		_bindless_object_desc_set = _bindless_desc_pool->allocate(_pipeline_bins.begin()->second->desc_set_layouts[DescriptorSetRate::PerObject]);
		_bindless_material_desc_set = _bindless_desc_pool->allocate(_pipeline_bins.begin()->second->desc_set_layouts[DescriptorSetRate::PerMaterial]);
	}
}

bool map_sampler_config(SamplerConfig config, otcv::SamplerBuilder& builder) {
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


VkFormat BindlessDataManager::choose_format(int channels, int bit_depth, bool srgb) {
	uint32_t format_index = otcv::pack((uint16_t)channels, (uint16_t)bit_depth) | (srgb ? 0x8000 : 0x0);
	auto iter = format_lut.find(format_index);
	if (iter == format_lut.end()) {
		return VK_FORMAT_UNDEFINED;
	}
	else {
		return iter->second;
	}
}

otcv::Image* BindlessDataManager::upload_image_async(ImageData& img_data, bool srgb, bool swizzle) {
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
	otcv::Image* image = new otcv::Image(imb);
	image->populate_async(img_data.pixel_data.data(), img_data.pixel_data.size(), otcv::ResourceState::FragSample);
	return image;
}

bool BindlessDataManager::set_materials(const MaterialResources& mat_res) {

	const std::vector<std::shared_ptr<ImageData>>& images_res = mat_res.images;
	const std::vector<SamplerConfig>& sampler_cfgs_res = mat_res.sampler_cfgs;
	const std::vector<TextureBinding>& textures_res = mat_res.textures;
	const std::vector<std::shared_ptr<MaterialData>>& materials_res = mat_res.materials;

	assert(materials_res.size() == _n_materials);
	assert(images_res.size() == _n_images);
	assert(sampler_cfgs_res.size() == _n_samplers);

	// upload images 
	_images.resize(mat_res.images.size(), nullptr);
	auto upload_image_by_texture_id = [&](int tex_id, bool srgb, bool swizzle) {
		if (tex_id < 0) {
			return;
		}
		int img_id = mat_res.textures[tex_id].image_id;
		if (img_id < 0) {
			return;
		}
		if (!_images[img_id]) {
			_images[img_id] = upload_image_async(*images_res[img_id], srgb, swizzle);
		}
	};
	for (std::shared_ptr<MaterialData> mat : materials_res) {
		upload_image_by_texture_id(mat->base_color_id, true, false);
		upload_image_by_texture_id(mat->normal_id, false, false);
		upload_image_by_texture_id(mat->metallic_roughness_id, false, true);
		upload_image_by_texture_id(mat->occlusion_id, false, false);
		upload_image_by_texture_id(mat->emissive_id, true, false);
	}
	// some images may not be referenced by any material
	std::vector<uint32_t> unreferenced_image_ids;
	for (uint32_t i = 0; i < _images.size(); ++i) {
		if (!_images[i]) {
			// This really should not happen. Why would a gltf file store images that are not referenced by anything
			unreferenced_image_ids.push_back(i);
			std::cout << "image index = " << i << " not referenced by any material. Upload to GPU anyway." << std::endl;
			_images[i] = upload_image_async(*images_res[i], false, false);
		}
	}

	// build samplers
	for (const SamplerConfig& cfg : sampler_cfgs_res) {
		otcv::SamplerBuilder sb;
		if (!map_sampler_config(cfg, sb)) {
			std::cout << "Failed to map sampler configuration." << std::endl;
			assert(false);
			return false;
		}
		_samplers.push_back(new otcv::Sampler(sb));
	}

	// bind image and sampler
	auto bind_image_sampler_by_texture_id = [&](int tex_id) {
		if (tex_id < 0) {
			return;
		}
		int img_id = textures_res[tex_id].image_id;
		if (img_id < 0) {
			return;
		}
		int sampler_id = textures_res[tex_id].sampler_id;
		if (sampler_id < 0) {
			return;
		}

		_bindless_material_desc_set->bind_sampled_image(1, &_images[img_id], img_id);
		_bindless_material_desc_set->bind_sampler(2, &_samplers[sampler_id], sampler_id);
	};
	for (std::shared_ptr<MaterialData> mat : materials_res) {
		bind_image_sampler_by_texture_id(mat->base_color_id);
		bind_image_sampler_by_texture_id(mat->metallic_roughness_id);
		bind_image_sampler_by_texture_id(mat->normal_id);
		bind_image_sampler_by_texture_id(mat->occlusion_id);
		bind_image_sampler_by_texture_id(mat->emissive_id);
	}
	assert(_samplers.size() > 0);
	// bind unreferenced images
	for (uint32_t unref_id : unreferenced_image_ids) {
		_bindless_material_desc_set->bind_image_sampler(1, &_images[unref_id], &_samplers[0], unref_id);
	}

	// build material ubos
	Std140AlignmentType MaterialCfg;
	MaterialCfg.add(Std140AlignmentType::InlineType::Vec4, "baseColorFactor");
	MaterialCfg.add(Std140AlignmentType::InlineType::Vec4, "mrnoFactor");
	MaterialCfg.add(Std140AlignmentType::InlineType::Uint, "alphaMode");
	MaterialCfg.add(Std140AlignmentType::InlineType::Float, "alphaCutoff");
	MaterialCfg.add(Std140AlignmentType::InlineType::Uint, "flipNormal");
	Std140AlignmentType TextureIds;
	TextureIds.add(Std140AlignmentType::InlineType::Int, "baseColorId");
	TextureIds.add(Std140AlignmentType::InlineType::Int, "normalId");
	TextureIds.add(Std140AlignmentType::InlineType::Int, "metallicRoughnessId");
	Std140AlignmentType SamplerIds;
	SamplerIds.add(Std140AlignmentType::InlineType::Int, "baseColorId");
	SamplerIds.add(Std140AlignmentType::InlineType::Int, "normalId");
	SamplerIds.add(Std140AlignmentType::InlineType::Int, "metallicRoughnessId");
	Std140AlignmentType MaterialUBO;
	MaterialUBO.add(MaterialCfg, "cfg");
	MaterialUBO.add(TextureIds, "texIds");
	MaterialUBO.add(SamplerIds, "samplerIds");
	_material_ubos.reset(new StaticUBOArray(MaterialUBO, materials_res.size(), _ubo_alignment));

	// upload material data to ubo
	for (uint32_t mat_id = 0; mat_id < materials_res.size(); ++mat_id) {
		MaterialData& mat_data = *materials_res[mat_id];
		_material_ubos->set(mat_id, StaticUBOAccess()["cfg"]["baseColorFactor"], &mat_data.base_color_factor);
		glm::vec4 mrno_factor(mat_data.metallic_factor, mat_data.roughness_factor, mat_data.normal_scale, mat_data.occlusion_strength);
		_material_ubos->set(mat_id, StaticUBOAccess()["cfg"]["mrnoFactor"], &mrno_factor);
		_material_ubos->set(mat_id, StaticUBOAccess()["cfg"]["alphaMode"], &mat_data.alpha_mode);
		_material_ubos->set(mat_id, StaticUBOAccess()["cfg"]["alphaCutoff"], &mat_data.alpha_cutoff);
		uint32_t flip_normal = mat_data.double_sided ? 1 : 0;
		_material_ubos->set(mat_id, StaticUBOAccess()["cfg"]["flipNormal"], &flip_normal);

		int image_id_write = -1;
		int sampler_id_write = -1;
		
		int bc_id = mat_data.base_color_id;
		int n_id = mat_data.normal_id;
		int mr_id = mat_data.metallic_roughness_id;

		image_id_write = bc_id >= 0 ? textures_res[bc_id].image_id : -1;
		sampler_id_write = bc_id >= 0 ? textures_res[bc_id].sampler_id : -1;
		_material_ubos->set(mat_id, StaticUBOAccess()["texIds"]["baseColorId"], &image_id_write);
		_material_ubos->set(mat_id, StaticUBOAccess()["samplerIds"]["baseColorId"], &sampler_id_write);

		image_id_write = n_id >= 0 ? textures_res[n_id].image_id : -1;
		sampler_id_write = n_id >= 0 ? textures_res[n_id].sampler_id : -1;
		_material_ubos->set(mat_id, StaticUBOAccess()["texIds"]["normalId"], &image_id_write);
		_material_ubos->set(mat_id, StaticUBOAccess()["samplerIds"]["normalId"], &sampler_id_write);

		image_id_write = mr_id >= 0 ? textures_res[mr_id].image_id : -1;
		sampler_id_write = mr_id >= 0 ? textures_res[mr_id].sampler_id : -1;
		_material_ubos->set(mat_id, StaticUBOAccess()["texIds"]["metallicRoughnessId"], &image_id_write);
		_material_ubos->set(mat_id, StaticUBOAccess()["samplerIds"]["metallicRoughnessId"], &sampler_id_write);
	}

	// bind ubo 
	_bindless_material_desc_set->bind_buffer_array(0, _material_ubos->_buf, 0, _material_ubos->_stride, _material_ubos->_n_ubos);

	// wait for async image uploads
	for (uint32_t i = 0; i < _images.size(); ++i) {
		_images[i]->wait_for_async();
	}
	return true;
}

void BindlessDataManager::set_objects(const SceneGraph& graph, const SceneGraphFlatRefs& graph_refs) {
	assert(_n_objects == graph_refs.size());

	// build index buffer
	size_t n_indices_total = 0;
	std::vector<uint32_t> obj_index_offsets;
	std::vector<uint32_t> obj_index_counts;
	for (const ObjectRef& obj_ref : graph_refs) {
		std::shared_ptr<MeshData> mesh = graph[obj_ref.node_id].renderables[obj_ref.renderable_id].mesh;
		obj_index_offsets.push_back(n_indices_total);
		obj_index_counts.push_back(mesh->indices.size());
		n_indices_total += obj_index_counts.back();

		assert(n_indices_total <= std::numeric_limits<uint32_t>::max());
	}

	std::vector<uint16_t> indices;
	indices.reserve(n_indices_total);

	for (const ObjectRef& obj_ref : graph_refs) {
		std::shared_ptr<MeshData> mesh = graph[obj_ref.node_id].renderables[obj_ref.renderable_id].mesh;
		indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
	}
	{
		otcv::BufferBuilder ibb;
		ibb.size(indices.size() * sizeof(uint16_t))
			.usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		_ib = new otcv::Buffer(ibb);
		_ib->populate(indices.data());
	}

	// build vertex buffer
	std::vector<int> obj_vertex_offsets;
	size_t n_vertices_total = 0;
	for (const ObjectRef& obj_ref : graph_refs) {
		std::shared_ptr<MeshData> mesh = graph[obj_ref.node_id].renderables[obj_ref.renderable_id].mesh;
		// sanity checks
		assert(!mesh->positions.empty());
		assert(mesh->positions.size() == mesh->normals.size() || mesh->normals.empty());
		assert(mesh->positions.size() == mesh->uv0.size() || mesh->uv0.empty());
		assert(mesh->positions.size() == mesh->tangents.size() || mesh->tangents.empty());

		obj_vertex_offsets.push_back(n_vertices_total);
		n_vertices_total += mesh->positions.size();

		// should not overflow
		assert(n_vertices_total <= std::numeric_limits<int>::max());
	}

	std::vector<glm::vec3> positions;
	positions.reserve(n_vertices_total);
	std::vector<glm::vec3> normals;
	normals.reserve(n_vertices_total);
	std::vector<glm::vec2> uv0;
	uv0.reserve(n_vertices_total);
	std::vector<glm::vec4> tangents;
	tangents.reserve(n_vertices_total);

	for (const ObjectRef& obj_ref : graph_refs) {
		std::shared_ptr<MeshData> mesh = graph[obj_ref.node_id].renderables[obj_ref.renderable_id].mesh;
		uint32_t n_vertices = mesh->positions.size();
		// position
		positions.insert(positions.end(), mesh->positions.begin(), mesh->positions.end());
		// normal
		if (!mesh->normals.empty()) {
			normals.insert(normals.end(), mesh->normals.begin(), mesh->normals.end());
		} else {
			normals.insert(normals.end(), n_vertices, glm::vec3(0.0f, 0.0f, 0.0f));
		}
		// uv0
		if (!mesh->uv0.empty()) {
			uv0.insert(uv0.end(), mesh->uv0.begin(), mesh->uv0.end());
		}
		else {
			uv0.insert(uv0.end(), n_vertices, glm::vec2(0.0f, 0.0f));
		}
		// tangent
		if (!mesh->tangents.empty()) {
			tangents.insert(tangents.end(), mesh->tangents.begin(), mesh->tangents.end());
		}
		else {
			tangents.insert(tangents.end(), n_vertices, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
	}
	{
		otcv::VertexBufferBuilder vb_builder;
		{
			// position
			otcv::BufferBuilder b_builder;
			b_builder
				.size(positions.size() * sizeof(glm::vec3))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder, positions.data());
			vb_builder.add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
		}
		{
			// normal
			otcv::BufferBuilder b_builder;
			b_builder
				.size(normals.size() * sizeof(glm::vec3))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder, normals.data());
			vb_builder.add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
		}
		{
			// uv0
			otcv::BufferBuilder b_builder;
			b_builder
				.size(uv0.size() * sizeof(glm::vec2))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder, uv0.data());
			vb_builder.add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
		}
		if (!tangents.empty()) {
			otcv::BufferBuilder b_builder;
			b_builder
				.size(tangents.size() * sizeof(glm::vec4))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder, tangents.data());
			vb_builder.add_attribute(3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
		}
		_vb = new otcv::VertexBuffer(vb_builder);
	}
	
	// build object ubos
	Std140AlignmentType ObjectUBO;
	ObjectUBO.add(Std140AlignmentType::InlineType::Mat4, "model");
	ObjectUBO.add(Std140AlignmentType::InlineType::Int, "matId");
	_object_ubos.reset(new StaticUBOArray(ObjectUBO, graph_refs.size(), _ubo_alignment));
	// upload object data to ubo
	for (uint32_t obj_id = 0; obj_id < graph_refs.size(); ++obj_id) {
		glm::mat4 model = graph[graph_refs[obj_id].node_id].world_transform;
		_object_ubos->set(obj_id, StaticUBOAccess()["model"], &model);
		int mat_id = graph[graph_refs[obj_id].node_id].renderables[graph_refs[obj_id].renderable_id].material_id;
		_object_ubos->set(obj_id, StaticUBOAccess()["matId"], &mat_id);
	}
	// bind object ubo
	_bindless_object_desc_set->bind_buffer_array(0, _object_ubos->_buf, 0, _object_ubos->_stride, _object_ubos->_n_ubos);

	// Build data segment
	assert(obj_index_counts.size() == obj_vertex_offsets.size());
	assert(obj_index_offsets.size() == obj_vertex_offsets.size());
	for (uint32_t i = 0; i < obj_index_counts.size(); ++i) {
		ObjectDataSegment segment;
		segment.index_start = obj_index_offsets[i];
		segment.index_count = obj_index_counts[i];
		segment.vertex_start = obj_vertex_offsets[i];
		_object_data_segment.push_back(segment);
	}
}