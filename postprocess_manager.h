#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "static_ubo.h"
#include "expandable_descriptor_pool.h"

class PostProcessManager {
public:
	PostProcessManager(const std::string& shader_path, otcv::Image* in_image, otcv::Image* out_image);
	~PostProcessManager();

	void commands(otcv::CommandBuffer* cmd_buf);

private:
	otcv::Image* _in_image;
	otcv::Image* _out_image;
	otcv::Sampler* _in_sampler;
	otcv::ShaderBlob _shader_blob;
	otcv::VertexBuffer* _screen_quad;
	otcv::GraphicsPipeline* _pipeline;
	std::shared_ptr<NaiveExpandableDescriptorPool> _desc_pool;
	otcv::DescriptorSet* _desc_set;
};
