#include "gltf_parser.h"
#include "otcv.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "gltf_traits.h"


#include <iostream>

namespace tg = tinygltf;

tg::Model model;
tg::TinyGLTF loader;
std::string err;
std::string warn;

std::vector<std::shared_ptr<ImageData>> g_image_datas;
std::vector<std::shared_ptr<TextureBinding>> g_textures;
std::vector<std::shared_ptr<MaterialData>> g_materials;
#ifdef GLTF_SCENE_USING_OTCV
std::shared_ptr<MaterialManager> g_mat_man = nullptr;
std::shared_ptr<DynamicUBOManager> g_object_ubo_man = nullptr;
#endif

template<typename T>
bool load_accessor(int accessor_id, std::vector<T>& buffer) {
	tg::Accessor& acc = model.accessors[accessor_id];
	tg::BufferView& bv = model.bufferViews[acc.bufferView];
	tg::Buffer& buf = model.buffers[bv.buffer];

	// type check
	if (GltfElementTraits<T>::gltf_type != acc.type ||
		GltfElementTraits<T>::gltf_component_type != acc.componentType) {
		std::cout << "data type mismatch. accessor.type = " << acc.type
			<< ", accessor.componentType = " << acc.componentType
			<< ", target type = " << TypeReflect<T>::name << std::endl;
		return false;
	}

	const unsigned char* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset;
	size_t count = acc.count;

	size_t element_size = sizeof(T);
	size_t stride = bv.byteStride ? bv.byteStride : element_size;

	buffer.resize(count);
	for (size_t i = 0; i < count; ++i) {
		const void* srcPtr = ptr + stride * i;
		std::memcpy(&buffer[i], srcPtr, sizeof(T));
	}

	return true;
}

template<typename T>
bool load_attribute(
	const tinygltf::Primitive& prim,
	const std::string& name,
	std::vector<T>& buffer,
	int mesh_id,
	int prim_id)
{
	auto iter = prim.attributes.find(name);
	if (iter != prim.attributes.end()) {
		if (!load_accessor(iter->second, buffer)) {
			std::cout << "error parsing attribute " << name << std::endl;
			return false;
		}
	}
	return true;
}

std::shared_ptr<ImageData> load_image(int image_id) {
	if (image_id < 0) {
		std::cout << "image id = " << image_id << std::endl;
		return nullptr;
	}
	assert(image_id < g_image_datas.size());
	if (g_image_datas[image_id]) {
		// this image is already loaded
		return g_image_datas[image_id];
	}

	// first time load
	tg::Image& gltf_image = model.images[image_id];
	g_image_datas[image_id].reset(new ImageData);
	std::shared_ptr<ImageData> i = g_image_datas[image_id];
	i->uri = gltf_image.uri;
	i->pixel_data = std::move(gltf_image.image);
	i->width = gltf_image.width;
	i->height = gltf_image.height;
	i->channels = gltf_image.component;
	i->bit_depth = gltf_image.bits;

	return i;
}

std::shared_ptr<TextureBinding> load_texture(int texture_id, int texcoord_id) {
	if (texture_id < 0) {
		return nullptr;
	}
	assert(texture_id < g_textures.size());
	if (g_textures[texture_id]) {
		return g_textures[texture_id];
	}

	const tg::Texture& gltf_texture = model.textures[texture_id];
	const tg::Sampler& gltf_sampler = model.samplers[gltf_texture.sampler];
	g_textures[texture_id].reset(new TextureBinding);
	std::shared_ptr<TextureBinding> t = g_textures[texture_id];
	t->name = gltf_texture.name;
	t->image = load_image(gltf_texture.source);
	if (!t->image) {
		std::cout << "fail to load image of texture. Texture id = " << texture_id << std::endl;
		assert(false); // it makes no sense for a texture to not point to an image
		g_textures[texture_id] = nullptr;
		return nullptr;
	}
	t->sampler_config.mag_filter = gltf_sampler.magFilter;
	t->sampler_config.min_filter = gltf_sampler.minFilter;
	t->sampler_config.wrap_s = gltf_sampler.wrapS;
	t->sampler_config.wrap_t = gltf_sampler.wrapT;
	t->texcoord_id = texcoord_id;
	return t;
}

std::shared_ptr<MaterialData> load_material(int material_id) {
	if (material_id < 0) {
		std::cout << "material id = " << material_id << std::endl;
		return nullptr;
	}
	assert(material_id < g_materials.size());
	if (g_materials[material_id]) {
		return g_materials[material_id];
	}

	const tg::Material& gltf_material = model.materials[material_id];
	g_materials[material_id].reset(new MaterialData);
	std::shared_ptr<MaterialData> m = g_materials[material_id];
	m->name = gltf_material.name;
	m->base_color_factor[0] = gltf_material.pbrMetallicRoughness.baseColorFactor[0];
	m->base_color_factor[1] = gltf_material.pbrMetallicRoughness.baseColorFactor[1];
	m->base_color_factor[2] = gltf_material.pbrMetallicRoughness.baseColorFactor[2];
	m->base_color_factor[3] = gltf_material.pbrMetallicRoughness.baseColorFactor[3];
	m->metallic_factor = gltf_material.pbrMetallicRoughness.metallicFactor;
	m->roughness_factor = gltf_material.pbrMetallicRoughness.roughnessFactor;
	m->normal_scale = gltf_material.normalTexture.scale;
	m->occlusion_strength = gltf_material.occlusionTexture.strength;
	if (gltf_material.alphaMode == "OPAQUE") {
		m->alpha_mode = AlphaMode::Opaque;
	}
	else if (gltf_material.alphaMode == "MASK") {
		m->alpha_mode = AlphaMode::Mask;
	}
	else if (gltf_material.alphaMode == "BLEND") {
		m->alpha_mode = AlphaMode::Blend;
	}
	else {
		std::cout << "Unrecognized alphaMode = " << gltf_material.alphaMode << std::endl;
		return nullptr;
	}
	m->alpha_cutoff = gltf_material.alphaCutoff;
	m->double_sided = gltf_material.doubleSided;

	m->base_color = load_texture(
		gltf_material.pbrMetallicRoughness.baseColorTexture.index,
		gltf_material.pbrMetallicRoughness.baseColorTexture.texCoord);
	m->metallic_roughness = load_texture(
		gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.index,
		gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
	m->normal = load_texture(
		gltf_material.normalTexture.index,
		gltf_material.normalTexture.texCoord);
	m->occlusion = load_texture(
		gltf_material.occlusionTexture.index,
		gltf_material.occlusionTexture.texCoord);
	m->emissive = load_texture(
		gltf_material.emissiveTexture.index,
		gltf_material.emissiveTexture.texCoord);

#ifdef GLTF_SCENE_USING_OTCV
	m->material_handle = g_mat_man->add_material(*m);
#endif

	return m;
}

#ifdef GLTF_SCENE_USING_OTCV
void upload_geometry(MeshData& mesh) {
	// upload index buffer
	mesh.ib = otcv::BufferBuilder()
		.size(mesh.indices.size() * sizeof(uint16_t))
		.usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.host_access(otcv::BufferBuilder::Access::Invisible)
		.build();
	mesh.ib->populate(mesh.indices.data());

	//upload vertex buffer
	otcv::VertexBufferBuilder vb_builder;
	uint32_t binding = 0;
	if (!mesh.positions.empty()) {
		otcv::BufferBuilder b_builder;
		b_builder
			.size(mesh.positions.size() * sizeof(glm::vec3))
			.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		vb_builder.add_binding(b_builder, mesh.positions.data());
		vb_builder.add_attribute(binding++, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
	}
	if (!mesh.normals.empty()) {
		otcv::BufferBuilder b_builder;
		b_builder
			.size(mesh.normals.size() * sizeof(glm::vec3))
			.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		vb_builder.add_binding(b_builder, mesh.normals.data());
		vb_builder.add_attribute(binding++, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
	}
	if (!mesh.uv0.empty()) {
		otcv::BufferBuilder b_builder;
		b_builder
			.size(mesh.uv0.size() * sizeof(glm::vec2))
			.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		vb_builder.add_binding(b_builder, mesh.uv0.data());
		vb_builder.add_attribute(binding++, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
	}
	if (!mesh.uv1.empty()) {
		otcv::BufferBuilder b_builder;
		b_builder
			.size(mesh.uv1.size() * sizeof(glm::vec2))
			.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		vb_builder.add_binding(b_builder, mesh.uv1.data());
		vb_builder.add_attribute(binding++, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
	}
	if (!mesh.tangents.empty()) {
		otcv::BufferBuilder b_builder;
		b_builder
			.size(mesh.tangents.size() * sizeof(glm::vec4))
			.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.host_access(otcv::BufferBuilder::Access::Invisible);
		vb_builder.add_binding(b_builder, mesh.tangents.data());
		vb_builder.add_attribute(binding++, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
	}
	mesh.vb = vb_builder.build();
}
#endif

// a gltf mesh primitive corresponds to a renderable
bool load_primitive(int mesh_id, int prim_id, Renderable& renderable) {
	const tg::Primitive& prim = model.meshes[mesh_id].primitives[prim_id];

	// load geometry
	renderable.mesh = std::make_shared<MeshData>();

	if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
		std::cout << "cannot process primitive mode = " << prim.mode << std::endl;
		return false;
	}

	if (!load_accessor(prim.indices, renderable.mesh->indices)) {
		std::cout << "error parsing indices. indices_id = " << prim.indices << std::endl;
		return false;
	}

	bool ret = true;
	ret &= load_attribute(prim, "POSITION", renderable.mesh->positions, mesh_id, prim_id);
	ret &= load_attribute(prim, "NORMAL", renderable.mesh->normals, mesh_id, prim_id);
	ret &= load_attribute(prim, "TANGENT", renderable.mesh->tangents, mesh_id, prim_id);
	ret &= load_attribute(prim, "TEXCOORD_0", renderable.mesh->uv0, mesh_id, prim_id);
	ret &= load_attribute(prim, "TEXCOORD_1", renderable.mesh->uv1, mesh_id, prim_id);

#ifdef GLTF_SCENE_USING_OTCV
	upload_geometry(*renderable.mesh);
#endif

	if (!ret) {
		return false;
	}
	
	renderable.material = load_material(prim.material);
	if (!renderable.material) {
		std::cout << "error loading material. Material id = " << prim.material << std::endl;
		return false;
	}

	return true;
}

glm::mat4 parse_matrix(const tg::Node& node) {
	glm::mat4 mat(1.0f);
	if (!node.matrix.empty()) {
		std::memcpy(&mat, node.translation.data(), sizeof(mat));
		return mat;
	}

	glm::vec3 t(0.0f);
	glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 s(1.0f);

	if (!node.translation.empty()) {
		std::memcpy(&t, node.translation.data(), sizeof(t));
	}

	if (!node.rotation.empty()) {
		r.x = node.rotation[0];
		r.y = node.rotation[1];
		r.z = node.rotation[2];
		r.w = node.rotation[3];
	}

	if (!node.scale.empty()) {
		s.x = node.scale[0];
		s.y = node.scale[1];
		s.z = node.scale[2];
	}

	glm::mat4 T = glm::translate(glm::mat4(1.0f), t);
	glm::mat4 R = glm::toMat4(r);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), s);
	
	return T * R * S;
}

bool load_node(int node_id, int parent_id, SceneGraph& scene) {
	const tg::Node& node = model.nodes[node_id];

	scene.emplace_back();
	SceneNode& scene_node = scene.back();
	int this_id = scene.size() - 1;

	scene_node.parent = parent_id;
	scene_node.name = node.name;
	scene_node.local_transform = parse_matrix(node);
	if (parent_id == -1) {
		scene_node.world_transform = scene_node.local_transform;
	}
	else {
		scene_node.world_transform = scene[parent_id].world_transform * scene_node.local_transform;
	}

	// parse primitives
	if (node.mesh != -1) {
		for (int prim_id = 0; prim_id < model.meshes[node.mesh].primitives.size(); ++prim_id) {
			scene_node.renderables.emplace_back();
			bool ret = load_primitive(node.mesh, prim_id, scene_node.renderables.back());
			if (!ret) {
				std::cout << "error loading primitive. mesh_id = " << node.mesh << ", prim_id = " << prim_id << std::endl;
				return false;
			}
#ifdef GLTF_SCENE_USING_OTCV
			MaterialManager::PipelineState& state = g_mat_man->get_pipeline_state(
				scene_node.renderables.back().material->material_handle,
				RenderPassType::Geometry);
			otcv::GraphicsPipeline* pipeline = g_mat_man->pipeline_cache->get(state.pipeline_handle);

			scene_node.object_ubo_set_handle = g_object_ubo_man->add_descriptor_set(pipeline, DescriptorSetRate::PerObjectUBO);
			g_object_ubo_man->set_value(scene_node.object_ubo_set_handle, "oUbo", "model", &scene_node.world_transform, sizeof(glm::mat4));
#endif
		}
	}

	// recursively parse children
	for (int node_id : node.children) {
		bool ret = load_node(node_id, this_id, scene);
		if (!ret) {
			std::cout << "error loading node. node_id = " << node_id << std::endl;
			return false;
		}
	}

	return true;
}

bool load_gltf(const std::string& filename, SceneGraph& scene) {
	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
	if (!err.empty()) {
		std::cout << "Error loading file " << filename << ": " << err << std::endl;
		return false;
	}
	if (!warn.empty()) {
		std::cout << "Warning loading file " << filename << ": " << warn << std::endl;
	}

	if (model.scenes.size() != 1) {
		std::cout << "Parse gltf file with 1 scene only" << std::endl;
		return false;
	}

	g_image_datas.resize(model.images.size(), nullptr);
	g_textures.resize(model.textures.size(), nullptr);
	g_materials.resize(model.materials.size(), nullptr);

	for (int node_id : model.scenes[0].nodes) {
		bool ret = load_node(node_id, -1, scene);
		if (!ret) {
			std::cout << "error loading node. node_id = " << node_id << std::endl;
			return false;
		}
	}

	// wait for all the async loads of images
	for (auto iter = g_mat_man->image_cache->begin(); iter != g_mat_man->image_cache->end(); ++iter) {
		iter->second->wait_for_async();
	}

	model = {};
	
	g_image_datas.clear();
	g_textures.clear();
	g_materials.clear();

	return true;
}

#ifdef GLTF_SCENE_USING_OTCV
bool load_gltf(const std::string& filename, SceneGraph& scene,
	std::shared_ptr<MaterialManager> mat_man, std::shared_ptr<DynamicUBOManager> object_ubo_man) {
	g_mat_man = mat_man;
	g_object_ubo_man = object_ubo_man;

	if (!load_gltf(filename, scene)) {
		g_object_ubo_man = nullptr;
		g_mat_man = nullptr;
		return false;
	}

	g_object_ubo_man = nullptr;
	g_mat_man = nullptr;
	return true;
}

BindOrder sort_draw_bind_order(SceneGraph& scene,
	std::shared_ptr<MaterialManager> mat_man,
	std::shared_ptr<DynamicUBOManager> object_ubo_man,
	RenderPassType pass_type) {

	BindOrder draw_order;
	for (size_t n_id = 0; n_id < scene.size(); ++n_id) {
		for (size_t r_id = 0; r_id < scene[n_id].renderables.size(); ++r_id) {
			std::shared_ptr<MaterialData> mat = scene[n_id].renderables[r_id].material;
			if (!mat) {
				continue;
			}
			MaterialHandle& mh = mat->material_handle;
			if (!mat_man->has_pass(mat->material_handle, pass_type)) {
				continue;
			}
			PipelineHandle& ph = mat_man->get_pipeline_state(mat->material_handle, pass_type).pipeline_handle;
			// try merge same pipeline
			auto p_iter = std::find_if(draw_order.begin(), draw_order.end(), [&](const PipelineBatch& batch) {
				return batch.pipeline == ph;
			});
			if (p_iter == draw_order.end()) {
				draw_order.emplace_back();
				p_iter = --draw_order.end();
				p_iter->pipeline = ph;
			}
			// try merge same material
			std::vector<MaterialBatch>& material_batches = p_iter->material_batches;
			auto m_iter = std::find_if(material_batches.begin(), material_batches.end(), [&](const MaterialBatch& batch) {
				return batch.material == mh;
			});
			if (m_iter == material_batches.end()) {
				material_batches.emplace_back();
				m_iter = --material_batches.end();
				m_iter->material = mh;
			}
			m_iter->renderables.push_back({ n_id, r_id });
			}
		}

	return draw_order;
}

#endif
