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

vec3 materialcolor()
{
    return vec3(1.);//vec3(material.r, material.g, material.b);
}

// Normal Distribution function --------------------------------------
float D_GGX(float dotNH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return (alpha2)/(PI * denom*denom);
}

// Geometric Shadowing function --------------------------------------
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

// Fresnel function ----------------------------------------------------
vec3 F_Schlick(float cosTheta, float metallic)
{
    vec3 F0 = mix(vec3(0.04), materialcolor(), metallic); // * material.specular
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
    return F;
}

// Specular BRDF composition --------------------------------------------

vec3 BRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness)
{
    // Precalculate vectors and dot products
    vec3 H = normalize (V + L);
    float dotNV = clamp(dot(N, V), 0.0, 1.0);
    float dotNL = clamp(dot(N, L), 0.0, 1.0);
    float dotLH = clamp(dot(L, H), 0.0, 1.0);
    float dotNH = clamp(dot(N, H), 0.0, 1.0);

    // Light color fixed
    vec3 lightColor = vec3(1.f);

    vec3 color = vec3(0.0);

    if (dotNL > 0.0)
    {
        float rroughness = max(0.05, roughness);
        // D = Normal distribution (Distribution of the microfacets)
        float D = D_GGX(dotNH, roughness);
        // G = Geometric shadowing term (Microfacets shadowing)
        float G = G_SchlicksmithGGX(dotNL, dotNV, rroughness);
        // F = Fresnel factor (Reflectance depending on angle of incidence)
        vec3 F = F_Schlick(dotNV, metallic);

        vec3 spec = D * F * G / (4.0 * dotNL * dotNV);

        color += spec * dotNL * lightColor;
    }

    return color;
}

void main()
{
    const vec3 dark_violet = vec3(0.59f, 0.0f, 0.82f);
    const vec3 chartreuse  = vec3(0.5f, 1.0f, 0.0f);

    vec3 lightColor = mix(dark_violet, chartreuse, 0.5f);
    if(Params.animateLightColor)
        lightColor = mix(dark_violet, chartreuse, abs(sin(Params.time)));

    mat4 lightMat = Params.lightMatrix;
    
    
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


    vec3 lightSpacePos = (lightMat * mViewInv * vec4(position, 1.)).xyz;

    float shadowmap_visibility = clamp(calculateShadow(lightSpacePos, 0.001f), 0., 1.);
    //shadowmap_visibility = texture(shadowmapTex, lightSpacePos * vec3(0.5f, 0.5f, 1.f) + vec3(0.5f, 0.5f, -0.010f));

    const float ambient_intensity = 0.05f;
    vec3 ambient = ambient_intensity * lightColor;
    vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;

    diffuse *= (1.f - ambient_intensity);

    out_fragColor = vec4((ambient + shadowmap_visibility * diffuse) * albedo, 1.);
}
