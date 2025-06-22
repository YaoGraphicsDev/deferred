#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "gltf_scene_bindless.h"
#include "static_ubo.h"
#include "expandable_descriptor_pool.h"
#include "camera.h"
#include "bindless_data_manager.h"


class SceneCulling {
public:
	SceneCulling(
		const std::string& shader_path,
		const SceneGraph& scene,
		const SceneGraphFlatRefs& scene_refs,
		uint32_t _in_flight_frames);

	~SceneCulling();

	// These 2 create functions create ssbo contexts that are not owned by this SceneCulling object
	// SSBOs should be kept externally and be passed back when pushing commands
	struct ObjectBufferContext {
		otcv::DescriptorSet* desc_set;
		std::shared_ptr<SSBO> ssbo;
	};
	ObjectBufferContext create_object_buffer_context(
		const SceneGraph& scene,
		const SceneGraphFlatRefs& scene_refs,
		std::shared_ptr<BindlessDataManager> bindless_data);

	struct IndirectCommandContext {
		otcv::DescriptorSet* desc_set;
		std::shared_ptr<SSBO> ssbo_commands;
		std::shared_ptr<SSBO> ssbo_draw_count;
	};
	IndirectCommandContext create_indirect_command_context(
		uint32_t n_pipeline_variants,
		std::shared_ptr<BindlessDataManager> bindless_data);

	void update(const glm::mat4& proj_inv, const glm::mat4& view_inv, uint32_t frame_id);

	void commands(
		otcv::CommandBuffer* cmd_buf,
		ObjectBufferContext in_context,
		IndirectCommandContext out_context,
		uint32_t frame_id);

private:
	otcv::ComputePipeline* _pipeline;
	otcv::ShaderBlob _shader_blob;
	std::shared_ptr<NaiveExpandableDescriptorPool> _desc_pool;

	struct FrameContext {
		otcv::DescriptorSet* _desc_set; // set 0, updated per frame
		std::shared_ptr<StaticUBO> _ubo;
	};
	std::vector<FrameContext> _frame_ctxs;

	uint32_t _n_obj;
	const uint32_t _compute_group_size = 64;
};