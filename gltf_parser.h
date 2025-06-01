#pragma once

#include "gltf_scene.h"

#include <string>

#ifdef GLTF_SCENE_USING_OTCV
#include "material_manager.h"
#endif

bool load_gltf(const std::string& filename, SceneGraph& scene);

#ifdef GLTF_SCENE_USING_OTCV
bool load_gltf(const std::string& filename, SceneGraph& scene,
	std::shared_ptr<MaterialManager> mat_man,
	std::shared_ptr<DynamicUBOManager> object_ubo_man);

BindOrder sort_draw_bind_order(SceneGraph& scene,
	std::shared_ptr<MaterialManager> mat_man,
	std::shared_ptr<DynamicUBOManager> object_ubo_man,
	RenderPassType pass_type);
#endif