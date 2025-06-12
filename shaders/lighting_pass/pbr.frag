#version 450

#define MAX_CASCADE_COUNT 6

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLit;

struct DirectionalLight {
    float intensity;
    vec3 color;
    vec3 direction;
};

struct Cascade {
    float zBegin;
    float zEnd;
    mat4 lightSpaceView;
    mat4 lightSpaceProject;
};

struct Shadow {
    vec2 nJitterTiles;
    uint nJitterStrataPerDim;
    float jitterRadius;
    float cascadeBlendDepth;
    uint nCascades;
    uint cascadeResolution;
    Cascade cascades[MAX_CASCADE_COUNT];
};

layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectInv;
    mat4 viewInv;
    DirectionalLight light; // allow only 1 directional light
    Shadow shadow;
} fUbo;

layout(set = 0, binding = 1) uniform sampler2D samplerDepth;
layout(set = 0, binding = 2) uniform sampler2D samplerAlbedo;
layout(set = 0, binding = 3) uniform sampler2D samplerNormal;
layout(set = 0, binding = 4) uniform sampler2D samplerMetallicRoughness;
// layout(set = 0, binding = 5) uniform sampler2DArrayShadow samplerCascadedShadow; // TODO: try use sampler2DArrayShadow
layout(set = 0, binding = 5) uniform sampler2DArray samplerCascadedShadow;
layout(set = 0, binding = 6) uniform sampler3D samplerJitter;

// 0.0 -- in shadow, 1.0 -- not in shadow
float shadow_factor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {

    vec4 lightClipSpaceCoord = lightProject * lightSpaceCoord;
    vec4 lightSpaceNDC = lightClipSpaceCoord * vec4(1.0f / lightClipSpaceCoord.w);
    vec2 shadowUV = (lightSpaceNDC.xy + vec2(1.0f)) * vec2(0.5f);
    float lightSpaceNDCZ = lightSpaceNDC.z;
	
    float cosTheta = dot(normal, lightDir);
    if (cosTheta <= 0.0f) {
        return 0.0f;
    }

    float shadowBias = max(0.0005 * (1.0 - cosTheta), 0.0001);
   
    // float shadowFactor = texture(samplerCascadedShadow, vec4(shadowUV, targetCascade, lightSpaceDepth - shadowBias));
    // return shadowFactor;

    float lightSpaceShadowNDCZ = texture(samplerCascadedShadow, vec3(shadowUV, targetCascade)).r;
    
	if (lightSpaceNDCZ - lightSpaceShadowNDCZ - shadowBias > 0.0f) {
		return 0.0f;
	} else {
		return 1.0f;
	}
}

vec4 ndc_to_view_space(vec4 ndc, mat4 projectInv) {
    vec4 viewSpaceCoord = projectInv * ndc;
    return viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
}

// return value < 0.0f -- no blocker, > 0 distance
float blocker_receiver_distance(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {
    
    vec4 lightClipSpaceCoord = lightProject * lightSpaceCoord;
    vec4 lightSpaceNDC = lightClipSpaceCoord * vec4(1.0f / lightClipSpaceCoord.w);
    vec2 shadowUV = (lightSpaceNDC.xy + vec2(1.0f)) * vec2(0.5f);
    float lightSpaceNDCZ = lightSpaceNDC.z;
	
    float cosTheta = dot(normal, lightDir);
    if (cosTheta <= 0.0f) {
        return 0.0f;
    }

    float shadowBias = max(0.0005 * (1.0 - cosTheta), 0.0001);
   
    // float shadowFactor = texture(samplerCascadedShadow, vec4(shadowUV, targetCascade, lightSpaceDepth - shadowBias));
    // return shadowFactor;

    float lightSpaceShadowNDCZ = texture(samplerCascadedShadow, vec3(shadowUV, targetCascade)).r;
    
	if (lightSpaceNDCZ - lightSpaceShadowNDCZ - shadowBias <= 0.0f) {
        // no blocker
		return -1.0f;
	}

    vec4 lightSpaceNDCBlocker = vec4(lightSpaceNDC.xy, lightSpaceShadowNDCZ, 1.0f);
    vec4 lightSpaceBlockerCoord = ndc_to_view_space(lightSpaceNDCBlocker, inverse(lightProject)); // TODO: do not compute light projection. Pass as uniform

    return abs(lightSpaceBlockerCoord.z - lightSpaceCoord.z);
}

bool uv_out_of_bound(vec2 uv) {
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)));
}


float pcf_shadow_factor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir,
    uint nStrata,
    vec2 nTiles,
    float jitterRadius) {

    uint nJitterSample = (nStrata * nStrata) / 2;
    uint nTestJitterSample = nStrata / 2;

    float jitterStepW = 1.0 / float(nJitterSample);

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);
    float shadowFactor = 0.0;

    // quick test to see if fully in light or shadow
    for (uint i = 0; i < nTestJitterSample; ++i) {
        vec4 jitter = texture(samplerJitter, jitterUVW);
        jitterUVW.z += jitterStepW;
    
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }
    
    float testAvg = shadowFactor / float(nStrata);
    if (testAvg < 0.0005 || testAvg > 0.9995) {
        return testAvg; // fully in shadow or light
    }
    
    // Reset shadowFactor, reuse jitterUVW.z, and continue with full sampling
    shadowFactor = testAvg * float(nStrata);

    for (uint i = 0; i < nJitterSample; ++i) {
        vec4 jitter = texture(samplerJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }

    return shadowFactor / float(nStrata * nStrata);
}

float pcss_shadow_factor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir,
    uint nStrata,
    vec2 nTiles,
    float jitterRadius) {

    uint nJitterSample = (nStrata * nStrata) / 2;
    uint nTestJitterSample = nStrata / 2;

    float jitterStepW = 1.0 / float(nJitterSample);

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);
    float shadowFactor = 0.0;

    // figure out jitter radius
    float blockerDistanceAvg = 0.0f;
    uint nBlocker = 0;
    for (uint i = 0; i < nTestJitterSample; ++i) {
        vec4 jitter = texture(samplerJitter, jitterUVW);
        jitterUVW.z += jitterStepW;
    
        float blockerDistance = blocker_receiver_distance(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        if (blockerDistance > 0.0f) {
            blockerDistanceAvg += blockerDistance;
            nBlocker += 1;
        }
        blockerDistance = blocker_receiver_distance(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        if (blockerDistance > 0.0f) {
            blockerDistanceAvg += blockerDistance;
            nBlocker += 1;
        }
    }
    if (nBlocker == 0) { 
        return 1.0f;
    } else if (nBlocker == nTestJitterSample * 2) {
		return 0.0f;
	} else {
        blockerDistanceAvg /= float(nBlocker);
    }

    jitterRadius = blockerDistanceAvg * 0.01f;

    for (uint i = 0; i < nJitterSample; ++i) {
        vec4 jitter = texture(samplerJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }

    return shadowFactor / float(nStrata * nStrata);
}


void main() {
    // world position
    float depth = texture(samplerDepth, inUV).r;
    vec4 ndc = vec4(inUV * 2.0f - 1.0f, depth, 1.0f);
    vec4 viewSpaceCoord = ndc_to_view_space(ndc, fUbo.projectInv); // fUbo.projectInv * ndc;
    // viewSpaceCoord = viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
    vec4 worldSpaceCoord = fUbo.viewInv * viewSpaceCoord;
	
	vec4 albedo = texture(samplerAlbedo, inUV);
    vec3 normal = texture(samplerNormal, inUV).xyz;
    normal = normalize(normal * vec3(2.0f) - vec3(1.0f));

    float zView = -viewSpaceCoord.z;
    uint targetCascade = 0;
    for (uint i = 0; i < fUbo.shadow.nCascades; ++i) {
        if (fUbo.shadow.cascades[i].zBegin < zView && fUbo.shadow.cascades[i].zEnd >= zView) {
            targetCascade = i;
            break;
        }
    }

    vec4 lightSpaceCoord0 = fUbo.shadow.cascades[targetCascade].lightSpaceView * worldSpaceCoord;
    float shadowFactor0 = pcf_shadow_factor(
                            targetCascade,
                            lightSpaceCoord0,
                            fUbo.shadow.cascades[targetCascade].lightSpaceProject,
                            normal,
                            -normalize(fUbo.light.direction),
                            fUbo.shadow.nJitterStrataPerDim,
                            fUbo.shadow.nJitterTiles,
                            fUbo.shadow.jitterRadius);

    float shadowFactor = shadowFactor0;
    // check if cascade blending is required
    if (fUbo.shadow.cascades[targetCascade].zEnd - zView < fUbo.shadow.cascadeBlendDepth &&
        targetCascade < fUbo.shadow.nCascades - 1) {

		vec4 lightSpaceCoord1 = fUbo.shadow.cascades[targetCascade + 1].lightSpaceView * worldSpaceCoord;
        float shadowFactor1 = pcf_shadow_factor(
                            targetCascade + 1,
                            lightSpaceCoord1,
                            fUbo.shadow.cascades[targetCascade + 1].lightSpaceProject,
                            normal,
                            -normalize(fUbo.light.direction),
                            fUbo.shadow.nJitterStrataPerDim,
                            fUbo.shadow.nJitterTiles,
                            fUbo.shadow.jitterRadius);
        float blendFactor = clamp(1.0f - (fUbo.shadow.cascades[targetCascade].zEnd - zView) / fUbo.shadow.cascadeBlendDepth, 0.0f, 1.0f);
        shadowFactor = mix(shadowFactor0, shadowFactor1, smoothstep(0.0f, 1.0f, blendFactor));
    }

    // TODO: temp, tranparent shadow to mimic GI
    shadowFactor = clamp(shadowFactor, 0.05f, 1.0f);

    
    vec3 diffuse = albedo.xyz * fUbo.light.color * vec3(fUbo.light.intensity) * max(dot(normal, -fUbo.light.direction), 0.0f);
    diffuse = diffuse * vec3(shadowFactor);
    
    if (targetCascade == 0) {
        diffuse = diffuse * vec3(1.7f, 0.6f, 0.6f);
    }
    if (targetCascade == 1) {
        diffuse = diffuse * vec3(0.6f, 1.7f, 0.6f);
    }
    if (targetCascade == 2) {
        diffuse = diffuse * vec3(0.6f, 0.6f, 1.7f);
    }

    outLit = vec4(diffuse, 1.0f);
}