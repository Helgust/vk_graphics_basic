#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes.h"

float HEIGHT = 64.0f;
float WIDTH = 64.0f;

layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
} params;


layout (location = 0) out VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;

} vOut;

layout(binding = 1) uniform sampler2D diffuseTexture;

out gl_PerVertex { vec4 gl_Position; };
void main(void)
{
    int iWidth = int(WIDTH);
    vec2 res = vec2((gl_VertexIndex % iWidth), (gl_VertexIndex / iWidth)); // x and z
    vec2 coord = vec2(res.x/WIDTH, res.y/HEIGHT);

    vec3 wNorm = vec3(0.0f, 1.0f, 0.0f);
    vec2 dx = vec2(1.0f, 0.0f) / WIDTH;
    vec2 dy = vec2(0.0f, 1.0f) / HEIGHT;

    if(res.x > 0.0f && res.x < WIDTH-1.0f && res.y > 0.0f && res.y < HEIGHT-1){
        vec2 r = coord + dx;
        float R = texture(diffuseTexture, r).x;
    
        vec2 l = coord - dx;
        float L = texture(diffuseTexture, l).x;
    
        vec2 t = coord - dy;
        float T = texture(diffuseTexture, t).x;
    
        vec2 b = coord + dy;
        float B = texture(diffuseTexture, b).x;
        
        wNorm = normalize(vec3((R - L) / 2.0f , -1.0f, (B - T) / 2.0f));
    }
    const vec4 wTang = vec4(DecodeNormal(floatBitsToInt(vTexCoordAndTang.z)), 0.0f);

    float offsetLeft = -10.0f;
    float offsetFront = -50.0f;
    float offsetLow = -5.0f;

    vOut.wPos     = vec3(res.x*2.0f + offsetLeft, texture(diffuseTexture, coord).x*10.0f + offsetLow, res.y*4.0f + offsetFront);
    vOut.wNorm    = -wNorm; 
    vOut.wTangent = vec3(0.0f, 0.0f, 0.0f);
    vOut.texCoord = coord; 

    gl_Position   = params.mProjView * vec4(vOut.wPos, 1.0f);
}