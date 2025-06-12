#pragma once

enum class RenderPassType {
	Shadow = 0,
	Geometry,
	Lighting,
	PostProcess,
	All
};

enum DescriptorSetRate {
	PerFrame = 0,
	PerObjectUBO = 1,
	PerMaterialUBO = 2,
	PerMaterialTexture = 3,
};

struct LightingModel {
	enum class Model {
		PBR,
		Lambert,
		Unlit
	};
	enum class Variant {
		Default,
		Emissive,
	};
	Model model = Model::Unlit;
	Variant variant = Variant::Default;
};
