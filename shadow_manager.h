#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "gltf_scene.h"
#include "expandable_descriptor_pool.h"
#include "dynamic_ubo_manager.h"
#include "static_ubo.h"
#include "camera.h"
#include "csm.h"

class ShadowManager {
public:
	// cascaded shadowmaps only
	ShadowManager(
		const std::string& shader_path,
		otcv::Image* shadowmap,
		std::shared_ptr<DynamicUBOManager> object_ubo_manager,
		uint32_t _in_flight_frames);
	~ShadowManager();

	// only allow 1 directional light at this point
	std::vector<CSM::CascadeContext> update(glm::vec3 light_dir, PerspectiveCamera& camera, uint32_t frame_id, float blend_overlap);

	void commands(otcv::CommandBuffer* cmd_buf, SceneGraph& scene, uint32_t frame_id);

	// std::vector<std::pair<float, float>> get_cascade_splits(uint32_t frame_id);

private:
	otcv::Image* _shadowmap;
	otcv::ShaderBlob _shader_blob;
	otcv::GraphicsPipeline* _pipeline;
	std::shared_ptr<NaiveExpandableDescriptorPool> _desc_pool;
	struct CascadeContext {
		otcv::DescriptorSet* desc_set;
		std::shared_ptr<StaticUBO> ubo;
		// std::pair<float, float> cascade_splits;
	};
	typedef std::vector<CascadeContext> FrameContext;
	std::vector<FrameContext> _frame_ctxs; // frame id -- cascade id
	std::shared_ptr<DynamicUBOManager> _object_ubo_manager;
};
