#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "gltf_scene_bindless.h"
#include "expandable_descriptor_pool.h"
#include "static_ubo.h"
#include "camera.h"
#include "csm.h"
#include "bindless_data_manager.h"
#include "scene_culling.h"

class ShadowManager {
public:
	// cascaded shadowmaps only
	ShadowManager(
		const std::string& shadow_shader_path,
		const std::string& culling_shader_path,
		otcv::Image* shadowmap,
		const SceneGraph& scene,
		const SceneGraphFlatRefs& scene_refs,
		std::shared_ptr<BindlessDataManager> bindless_data,
		uint32_t in_flight_frames);
	~ShadowManager();

	// only allow 1 directional light at this point
	std::vector<CSM::CascadeContext> update(glm::vec3 light_dir, PerspectiveCamera& camera, uint32_t frame_id, float blend_overlap);

	void commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id);

	// std::vector<std::pair<float, float>> get_cascade_splits(uint32_t frame_id);

private:
	otcv::Image* _shadowmap;
	otcv::ShaderBlob _shader_blob;
	std::map<PipelineVariant, otcv::GraphicsPipeline*> _pipeline_bins;
	std::shared_ptr<NaiveExpandableDescriptorPool> _desc_pool;
	struct CascadeContext {
		otcv::DescriptorSet* desc_set;
		std::shared_ptr<StaticUBO> ubo;
		// std::pair<float, float> cascade_splits;
	};
	typedef std::vector<CascadeContext> FrameContext;
	std::vector<FrameContext> _frame_ctxs; // frame id -- cascade id

	std::shared_ptr<BindlessDataManager> _bindless_data;

	std::vector<std::shared_ptr<SceneCulling>> _scene_cullings; // one cull per cascade
	SceneCulling::ObjectBufferContext _culling_in;
	std::vector<SceneCulling::IndirectCommandContext> _culling_out;
	uint32_t _n_obj;
};
