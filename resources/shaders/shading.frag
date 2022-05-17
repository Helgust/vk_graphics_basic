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


vec3 materialcolor()
{
    return vec3(1.);//vec3(material.r, material.g, material.b);
}


vec4 sss(float thickness, float attentuation, vec3 normal) {
    vec3 light = normalize((normal * distortion));
    float dot1 = pow(saturate(dot(vec3(0,0,-1), -light)), 2) * 5;
    float lt = attentuation * (dot1 + 3) * thickness;
    //lt = 1.0 - lt;
    vec4 res = vec4(1.0f,1.0f,1.0f,1.0f) * lt;
    return res;
}


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
        subpassLoad(inDepth).x,
        1.0);
    screenSpacePos.y = 1.0f;

    vec4 camSpacePos = inverse(projMat) * screenSpacePos;
    
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
    float lightDist = length(vec3(0.0,0.5,0.0) - position);
    const float ambient_intensity = 0.05f;
    vec3 ambient = ambient_intensity * lightColor;
    vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;
    diffuse *= (1.f - ambient_intensity);

     float thickness = abs(texture(shadowmapTex, lightSpacePos * vec3(0.5f, 0.5f, 1.f) + vec3(0.5f, 0.5f, -0.010f)));

    if (thickness <= 0.0) {
        discard;
    }

    thickness = ((1.0 - thickness) * 0.05);
    vec4 sss_col = sss(thickness, 3.0f, normal);
    out_fragColor = vec4((ambient + shadowmap_visibility * diffuse) * albedo, 1.) + sss_col;
}