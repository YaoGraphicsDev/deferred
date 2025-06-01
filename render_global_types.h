#pragma once

enum class RenderPassType {
	Geometry = 0,
	Lighting,
	All
};

enum DescriptorSetRate {
	PerFrameUBO = 0,
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
