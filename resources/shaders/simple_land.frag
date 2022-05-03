#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "common.h"
#include "noise.glsl"

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

mat4 invViewProj = inverse(params.mProjView);

vec4 screenToWorld(vec2 pos, float depth)
{
    const vec4 sPos = vec4(2.0 * pos - 1.0, depth, 1.0);
    const vec4 wPos = invViewProj * sPos;
    return wPos / wPos.w;
}

float fogDensity(vec3 pos)
{
    float max_height = Params.fogHeight/20.0f;
    float height_mult = clamp(exp(-pos.y / max_height) , 0, 1);
    return clamp(((cnoise(pos /  Params.fogDensity  + vec3(Params.time, 0, Params.time) / 1) + 1) /2) * 0.03f, 0, 1) * height_mult;
}

float shade()
{   
    float offsetLeft = -10.0f;
    float offsetFront = -50.0f;
    float offsetLow = -5.0f;
 
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

    const float minHits = 60.0f;
    return (minHits - min(result, minHits))/minHits;
}

void main()
{
    float divider = 150.0f;
    float area = 100.0f;
    float multiplier = 5.0 * 50;
    vec3 lightDir1 = normalize(Params.lightPos - surf.wPos);
    vec3 lightDir2 = vec3(0.0f, 0.0f, 1.0f);

    vec4 lightColor1 = vec4(0.67f, 0.86f, 0.89f, 1.0f);
    vec4 lightColor2 = vec4(0.92f, 0.71f, 0.46f, 1.0f);

    vec3 N = surf.wNorm; 

    vec4 color1 = max(dot(N, lightDir1), 0.0f) * lightColor1;
    vec4 color2 = max(dot(N, lightDir2), 0.0f) * lightColor2;
    vec4 color_lights = mix(color1, color2, 0.5f);
    
    //fog 
    const vec2 fragPos = gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight);

    const vec4 wCamPos = screenToWorld(vec2(0.5), 0);
    vec3 surfPos = surf.wPos;
    vec3 dirFog = normalize(surfPos - wCamPos.xyz);

    vec3 pos = screenToWorld(fragPos.xy, 0).xyz;

    float stepFog = Params.fogStep/10.0f;
    float translucency = 1;
    vec3 fogColor = vec3(0);
    vec3 dirLight = lightDir1;
    const vec4 baseFogColor = vec4(Params.fogColor, 0.);


    for (uint i = 0; i < Params.fogStepNum; ++i)
    {
        float beersTerm = exp(-fogDensity(pos) * stepFog);
        fogColor += translucency * baseFogColor.xyz * (1 - beersTerm) * 0.5f;

        translucency *=  beersTerm;
        if (dot(pos, dirFog) > dot(surfPos, dirFog) || translucency < 0.0001f)
        {
            break;
        }
        pos += dirFog * stepFog;
    }
    
    vec4 pre_color = vec4(color_lights.xyz*max(shade(), 0.1f), 1.0f);// * vec4(texture(diffuseTexture, surf.texCoord).xyz, 1.0f);
    if(Params.enableFog)
    {
        out_fragColor = vec4((translucency * pre_color.xyz + fogColor),1.0f);
        //vec4 f_color  = vec4(fogColor,translucency);
        //out_fragColor = mix(pre_color, f_color, 0.5);
    }
    else
    {
        out_fragColor = pre_color;
    }
    
}