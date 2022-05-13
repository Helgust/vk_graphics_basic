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


layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inTangent;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout (input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

layout (location = 0) in vec2 outUV;

float sq(float x) { return x*x; }

void main()
{
    const vec3 dark_violet = vec3(0.59f, 0.0f, 0.82f);
    const vec3 chartreuse  = vec3(0.5f, 1.0f, 0.0f);

    vec3 lightColor = mix(dark_violet, chartreuse, 0.5f);
    // if(Params.animateLightColor)
    //     lightColor = mix(dark_violet, chartreuse, abs(sin(Params.time)));

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
    vec3 lightDir = normalize(transpose(mat3(lightMat) * mat3(mViewInv)) * vec3(0., 0., 1.));

    //if (gl_FragCoord.x == 0.5 && gl_FragCoord.y == 0.5)
    //    debugPrintfEXT("lightDir: %v3f\n", lightDir.xyz);


    vec3 lightSpacePos = (lightMat * mViewInv * vec4(position, 1.)).xyz;

    vec3 ambient = vec3(0.05f) * lightColor;
    vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;

    diffuse *= (1.f - ambient);

    out_fragColor = vec4((ambient + diffuse) * albedo, 1.);
}
