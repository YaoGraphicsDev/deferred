#include "gltf_parser_bindless.h"
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

std::vector<std::shared_ptr<ImageData>> g_images;
std::vector<SamplerConfig> g_sampler_cfgs;
std::vector<TextureBinding> g_textures;
std::vector<std::shared_ptr<MaterialData>> g_materials;

template<typename T>
static bool load_accessor(int accessor_id, std::vector<T>& buffer) {
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
static bool load_attribute(
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

static std::shared_ptr<ImageData> load_image(int image_id) {
	if (image_id < 0) {
		std::cout << "image id = " << image_id << std::endl;
		return nullptr;
	}

	tg::Image& gltf_image = model.images[image_id];
	std::shared_ptr<ImageData> i = std::make_shared<ImageData>();
	i->uri = gltf_image.uri;
	i->pixel_data = std::move(gltf_image.image);
	i->width = gltf_image.width;
	i->height = gltf_image.height;
	i->channels = gltf_image.component;
	i->bit_depth = gltf_image.bits;

	return i;
}

static bool load_all_images() {
	for (int image_id = 0; image_id < model.images.size(); ++image_id) {
		std::shared_ptr<ImageData> image = load_image(image_id);
		if (!image) {
			std::cout << "Failed to load image." << std::endl;
			return false;
		}
		g_images.push_back(image);
	}
	return true;
}

static void load_all_samplers() {
	for (tg::Sampler& gltf_sampler : model.samplers) {
		SamplerConfig sampler_cfg;
		sampler_cfg.mag_filter = gltf_sampler.magFilter;
		sampler_cfg.min_filter = gltf_sampler.minFilter;
		sampler_cfg.wrap_s = gltf_sampler.wrapS;
		sampler_cfg.wrap_t = gltf_sampler.wrapT;

		g_sampler_cfgs.push_back(sampler_cfg);
	}
}

static void setup_all_textures() {
	for (tg::Texture& gltf_texture : model.textures) {
		g_textures.push_back({ gltf_texture.source, gltf_texture.sampler });
	}
}

static std::shared_ptr<MaterialData> load_material(int material_id) {
	if (material_id < 0) {
		std::cout << "material id = " << material_id << std::endl;
		return nullptr;
	}

	const tg::Material& gltf_material = model.materials[material_id];
	std::shared_ptr<MaterialData> m = std::make_shared<MaterialData>();
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
		std::cout << "Unrecognized alphaMode = " << gltf_material.alphaMode << ", material id = " << material_id << std::endl;
		return nullptr;
	}
	m->alpha_cutoff = gltf_material.alphaCutoff;
	m->double_sided = gltf_material.doubleSided;

	m->base_color_id = gltf_material.pbrMetallicRoughness.baseColorTexture.index;
	m->metallic_roughness_id = gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.index;
	m->normal_id = gltf_material.normalTexture.index;
	m->occlusion_id = gltf_material.occlusionTexture.index;
	m->emissive_id = gltf_material.emissiveTexture.index;

	return m;
}

static bool load_all_materials() {
	for (int material_id = 0; material_id < model.materials.size(); ++material_id) {
		std::shared_ptr<MaterialData> material = load_material(material_id);
		if (!material) {
			std::cout << "Failed to load material." << std::endl;
			return false;
		}
		g_materials.push_back(material);
	}
	return true;
}

// a gltf mesh primitive corresponds to a renderable
static bool load_primitive(int mesh_id, int prim_id, Renderable& renderable) {
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

	if (!ret) {
		return false;
	}
	
	renderable.material_id = prim.material;
	return true;
}

static glm::mat4 parse_matrix(const tg::Node& node) {
	glm::mat4 mat(1.0f);
	if (!node.matrix.empty()) {
		for (int i = 0; i < 16; i++) {
			reinterpret_cast<float*>(&mat)[i] = static_cast<float>(node.matrix[i]);
		}
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

static bool load_node(int node_id, int parent_id, SceneGraph& scene) {
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

static bool load_gltf(const std::string& filename, SceneGraph& scene) {
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

	for (int node_id : model.scenes[0].nodes) {
		bool ret = load_node(node_id, -1, scene);
		if (!ret) {
			std::cout << "error loading node. node_id = " << node_id << std::endl;
			return false;
		}
	}

	if (!load_all_images()) {
		std::cout << "error loading all images" << std::endl;
		return false;
	}
	setup_all_textures();
	load_all_samplers();
	if (!load_all_materials()) {
		std::cout << "error loading all materials" << std::endl;
		return false;
	}

	model = {};
	loader = {};
	err.clear();
	warn.clear();

	return true;
}

static PipelineVariant find_pipeline_variant(SceneGraph& scene, uint32_t node_id, uint32_t renderable_id,  MaterialResources& mat_res) {
	std::shared_ptr<MaterialData> mat = mat_res.materials[scene[node_id].renderables[renderable_id].material_id];
	if (mat->double_sided) {
		return PipelineVariant::DoubleSided;
	} else {
		return PipelineVariant::BackFaceCulled;
	}
}

bool load_gltf(
	const std::string& filename,
	SceneGraph& graph,
	SceneGraphFlatRefs& graph_refs,
	MaterialResources& mat_res) {

	if (!load_gltf(filename, graph)) {
		std::cout << "error loading gltf file " << filename << std::endl;
		// clear g_* buffers
		assert(false);
		return false;
	}

	mat_res.images = std::move(g_images);
	mat_res.sampler_cfgs = std::move(g_sampler_cfgs);
	mat_res.textures = std::move(g_textures);
	mat_res.materials = std::move(g_materials);

	g_images.clear();
	g_sampler_cfgs.clear();
	g_textures.clear();
	g_materials.clear();

	for (uint32_t node_id = 0; node_id < graph.size(); ++node_id) {
		SceneNode& node = graph[node_id];
		for (uint32_t renderable_id = 0; renderable_id < node.renderables.size(); ++renderable_id) {
			if (graph[node_id].renderables[renderable_id].mesh) {
				PipelineVariant variant = find_pipeline_variant(graph, node_id, renderable_id, mat_res);
				graph_refs.push_back({ node_id, renderable_id, variant });
			}
		}
	}

	return true;
}