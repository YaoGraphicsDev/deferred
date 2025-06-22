#pragma once

#include "gltf_scene_bindless.h"
#include "bindless_data_manager.h"

#include <string>

bool load_gltf(
	const std::string& filename,
	SceneGraph& graph,
	SceneGraphFlatRefs& graph_refs,
	MaterialResources& mat_res);
