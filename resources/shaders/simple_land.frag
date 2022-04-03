#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "common.h"

float height = 64.0f;
float width = 64.0f;

layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
} params;

layout (location = 0 ) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 1) uniform sampler2D diffuseTexture;

float shade()
{   
    float offsetLeft = -10.0f;
    float offsetFront = -50.0f;
    float offsetLow = -5.0f;

    //const vec3 mLightPos = Params.lightPos;
    // const vec3 start = globCoord;

 
    const float step_ = 0.1f;
    vec3 dir = normalize(Params.lightPos - surf.wPos);

    float result = 0.0f;

    vec3 current = surf.wPos;
    while (current.x >= offsetLeft  && current.x <= width*2.0f  + offsetLeft
        && current.y >= offsetLow   && current.y <= 255.0f
        && current.z >= offsetFront && current.z <= height*2 + offsetFront)
    {
        current += step_*dir;
        vec2 coord = vec2((current.x-offsetLeft)/(2.0f*width), (current.y-offsetFront)/(2.0f*height));
        float high = texture(diffuseTexture, coord).x*10.0f + offsetLow;

        if (current.y < high)
            result += 1.f;
    }

    vec2 coord = vec2((current.x-offsetLeft)/(2.0f*width), (current.y-offsetFront)/(2.0f*height));
    float high = texture(diffuseTexture, coord).x*10.0f + offsetLow;

    if (current.y < high)
        result += 1.f;

    const float minHits = 30.0f;
    return (minHits - min(result, minHits))/minHits;
}

void main()
{
    float area = 100.0f;
    vec3 lightDir1 = normalize(Params.lightPos - surf.wPos);
    vec3 lightDir2 = vec3(0.0f, 0.0f, 1.0f);

    vec4 lightColor1 = vec4(0.67f, 0.86f, 0.89f, 1.0f);
    vec4 lightColor2 = vec4(0.92f, 0.71f, 0.46f, 1.0f);

    vec3 N = surf.wNorm; 

    vec4 color1 = max(dot(N, lightDir1), 0.0f) * lightColor1;
    vec4 color2 = max(dot(N, lightDir2), 0.0f) * lightColor2;
    vec4 color_lights = mix(color1, color2, 0.5f);
    

    out_fragColor = vec4(color_lights.xyz*max(shade(), 0.5f), 1.0f);// * vec4(texture(diffuseTexture, surf.texCoord).xyz, 1.0f);
}