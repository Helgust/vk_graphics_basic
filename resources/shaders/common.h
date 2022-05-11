#ifndef VK_GRAPHICS_BASIC_COMMON_H
#define VK_GRAPHICS_BASIC_COMMON_H

#ifdef __cplusplus
#include <LiteMath.h>
using LiteMath::uint2;
using LiteMath::float2;
using LiteMath::float3;
using LiteMath::float4;
using LiteMath::float4x4;
using LiteMath::make_float2;
using LiteMath::make_float4;

typedef unsigned int uint;
typedef uint2        uvec2;
typedef float4       vec4;
typedef float3       vec3;
typedef float2       vec2;
typedef float4x4     mat4;
#endif

struct UniformParams
{
  mat4 proj;
  mat4 view;
  mat4 lightMatrix;
  vec3 lightAngle;
  float time;
  vec3 baseColor;
  float screenWidth;
  float screenHeight;
  bool animateLightColor;
};

struct PushConst
{
  mat4 model;
  vec4 color;
  uint instanceID;
};

#endif //VK_GRAPHICS_BASIC_COMMON_H
