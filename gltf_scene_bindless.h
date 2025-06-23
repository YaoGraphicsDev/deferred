#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

#include "gltf_scene_config.h"

#include "otcv.h"
#include "global_handles.h"
#include "render_global_types.h"

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uv0;
    std::vector<glm::vec2> uv1;
    std::vector<glm::vec4> tangents;

    std::vector<uint16_t> indices;

    AABB aabb;
};

struct ImageData {
    std::string uri;
    std::vector<uint8_t> pixel_data;
    int width;
    int height;
    int channels;
    int bit_depth;
};

struct SamplerConfig {
    int min_filter;
    int mag_filter;
    int wrap_s;
    int wrap_t;
};

struct TextureBinding {
    int image_id;
    int sampler_id;
};

enum class AlphaMode {
    Opaque = 0,
    Mask,
    Blend
};

struct MaterialData {
    std::string name;

    glm::vec4 base_color_factor = glm::vec4(1.0f);
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 0.0f;
    
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;

    int base_color_id;
    int metallic_roughness_id;
    int normal_id;
    int occlusion_id;
    int emissive_id;
};

struct Renderable {
    std::shared_ptr<MeshData> mesh;
    int material_id;
};

struct SceneNode {
    int parent = -1;
    std::string name;
    glm::mat4 local_transform = glm::mat4(1.0f);
    glm::mat4 world_transform = glm::mat4(1.0f);
    std::vector<Renderable> renderables;
};

typedef std::vector<SceneNode> SceneGraph;

enum class PipelineVariant : uint32_t {
    // Geometry pass
    BackFaceCulled = 0,
    DoubleSided = 1,
    All = 2
};
struct ObjectRef {
    uint32_t node_id;
    uint32_t renderable_id;
    PipelineVariant pipeline_variant;
};
typedef std::vector<ObjectRef> SceneGraphFlatRefs;

struct MaterialResources {
    std::vector<std::shared_ptr<ImageData>> images;
    std::vector<SamplerConfig> sampler_cfgs;
    std::vector<TextureBinding> textures;
    std::vector<std::shared_ptr<MaterialData>> materials;
};
