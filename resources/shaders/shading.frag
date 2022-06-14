#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "common.h"



layout(location = 0) out vec4 out_fragColor;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout (binding = 1) uniform sampler2DShadow shadowmapTex;

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inTangent;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout (input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

vec3 lightPos = vec3(0.0f,0.5f,0.0f);

float saturate(float val) {
    return clamp(val, 0.0, 1.0);
}

const float trans_coef = 0.5;

float width(const vec4 lightSpacePos, const float bias){
    float d1 = texture(shadowmapTex, vec3(lightSpacePos.xy * 0.5 + 0.5, 0));
    float d2 = lightSpacePos.z;
    return abs(d1 - d2);
}

// This function can be precomputed for efficiency
vec3 T(float s) {
    return vec3(0.233, 0.455, 0.649) * exp(-s*s/0.0064) + \
           vec3(0.1, 0.336, 0.344) * exp(-s*s/0.0484) + \
           vec3(0.118, 0.198, 0.0) * exp(-s*s/0.187) + \
           vec3(0.113, 0.007, 0.007) * exp(-s*s/0.567) + \
           vec3(0.358, 0.004, 0.0) * exp(-s*s/1.99) + \
           vec3(0.078, 0.0, 0.0) * exp(-s*s/7.41);
}

layout (location = 0) in vec2 outUV;

float sq(float x) { return x*x; }

float calculateShadow(const vec3 lightSpacePos, const float bias) {
    vec3 pos_proj = lightSpacePos * vec3(0.5f, 0.5f, 1.f) + vec3(0.5f, 0.5f, -bias);

    float shadow_opacity = 0;

    vec2 texelSize = 1.0 / textureSize(shadowmapTex, 0);

    for (int x = -1; x <= 2; ++x) {
        for (int y = -1; y <= 2; ++y) {
            shadow_opacity += texture(shadowmapTex, vec3(pos_proj.xy + (vec2(x, y) - vec2(0.5)) * texelSize, pos_proj.z));
        }
    }
    shadow_opacity /= 16;

    /*vec2 offset = vec2(greaterThan(fract(gl_FragCoord.xy * 0.5), vec2(0.25)));
    // mod
    offset.y += offset.x;
    // y ^= x in floating point
    if (offset.y > 1.1)
        offset.y = 0;

    shadow_opacity =
        ( texture(shadowmapTex, vec3(pos_proj.xy + (offset + vec2(-1.5,  0.5)) * texelSize, pos_proj.z))
        + texture(shadowmapTex, vec3(pos_proj.xy + (offset + vec2( 0.5,  0.5)) * texelSize, pos_proj.z))
        + texture(shadowmapTex, vec3(pos_proj.xy + (offset + vec2(-1.5, -1.5)) * texelSize, pos_proj.z))
        + texture(shadowmapTex, vec3(pos_proj.xy + (offset + vec2( 0.5, -1.5)) * texelSize, pos_proj.z))) * 0.25;*/

    float pos_depth = pos_proj.z;
    return pos_depth >= 1. ? 0 : shadow_opacity;
}

const float PI = 3.14159265359;
const float DEG_TO_RAD = PI / 180.0;
float distortion = 12;


void main()
{
    const vec3 dark_violet = vec3(0.59f, 0.0f, 0.82f);
    const vec3 chartreuse  = vec3(0.5f, 1.0f, 0.0f);

    vec3 lightColor = mix(dark_violet, chartreuse, 0.5f);
    if(Params.animateLightColor)
        lightColor = mix(dark_violet, chartreuse, abs(sin(Params.time)));

    mat4 lightMat = Params.lightMatrix;
    mat4 projMat  = Params.proj;

    //debugPrintfEXT("lightDir: %v3f\n", projMat);

    vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);

    vec4 camSpacePos = inverse(Params.proj) * screenSpacePos;
    
    vec3 position = camSpacePos.xyz / camSpacePos.w;
    vec3 normal = subpassLoad(inNormal).xyz;
    vec3 tangent = subpassLoad(inTangent).xyz;
    vec3 albedo = subpassLoad(inAlbedo).rgb;

    mat4 mViewInv = inverse(Params.view);

    // from lightspace to screenspace
    vec3 lightDir = -normalize(transpose(mat3(lightMat) * mat3(mViewInv)) * vec3(0., 0., 1.));

    //if (gl_FragCoord.x == 0.5 && gl_FragCoord.y == 0.5)
    //    debugPrintfEXT("lightDir: %v3f\n", lightDir.xyz);


    vec4 lightSpacePos = (lightMat * mViewInv * vec4(position, 1.));

    float shadowmap_visibility = clamp(calculateShadow(lightSpacePos.xyz, 0.001f), 0., 1.);
    //shadowmap_visibility = texture(shadowmapTex, lightSpacePos * vec3(0.5f, 0.5f, 1.f) + vec3(0.5f, 0.5f, -0.010f));
    const float ambient_intensity = 0.3f;
    vec3 ambient = ambient_intensity * lightColor;
    vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;
    diffuse *= (1.f - ambient_intensity);

    float s = width(lightSpacePos, 0.0f);
    float E = max(0.3 + dot(-normal, lightDir), 0.0);
    vec3 transmittance = T(s) * albedo * E * trans_coef;

    vec3 M = (ambient + diffuse) * albedo * shadowmap_visibility;
    if (Params.enableSss) {
      M += transmittance;
    }

    out_fragColor = vec4(M , 1.);
}