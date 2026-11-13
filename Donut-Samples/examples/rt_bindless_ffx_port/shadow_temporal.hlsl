#include "shadow_cb.h"
#include "ffx/soft_color_clamp.hlsl"

Texture2D<float> t_Vis : register(t0);
Texture2D<uint> t_ShadowMask : register(t1);
Texture2D<float4> t_PrevMoments : register(t2);
Texture2D<float4> t_PrevAccum : register(t3);
Texture2D<float4> t_Reproj : register(t4);
Texture2D<float> t_Depth : register(t5);
Texture2D<float4> t_Normal : register(t6);
Texture2D<float> t_PrevDepth : register(t7);

RWTexture2D<float4> u_Moments : register(u0);
RWTexture2D<float2> u_TemporalOut : register(u1);
RWTexture2D<uint> u_TileMeta : register(u2);

SamplerState s_LinearClamp : register(s0);

ConstantBuffer<ShadowDenoiseConstants> g_Shadow : register(b0);

uint2 FFX_DNSR_Shadows_GetBufferDimensions()
{
    return uint2(g_Shadow.texSize.xy);
}

float2 FFX_DNSR_Shadows_GetInvBufferDimensions()
{
    return g_Shadow.texSize.zw;
}

float4x4 FFX_DNSR_Shadows_GetProjectionInverse()
{
    return transpose(g_Shadow.matClipToView);
}

float4x4 FFX_DNSR_Shadows_GetReprojectionMatrix()
{
    return transpose(mul(g_Shadow.matClipToWorld, g_Shadow.prevViewProj));
}

float4x4 FFX_DNSR_Shadows_GetViewProjectionInverse()
{
    return transpose(g_Shadow.matClipToWorld);
}

float3 FFX_DNSR_Shadows_GetEye()
{
    return g_Shadow.matViewToWorld[3].xyz;
}

float3 FFX_DNSR_Shadows_ReadNormals(uint2 px)
{
    return t_Normal.Load(int3(px, 0)).xyz;
}

float FFX_DNSR_Shadows_ReadDepth(uint2 px)
{
    return t_Depth.Load(int3(px, 0));
}

float FFX_DNSR_Shadows_ReadPreviousDepth(uint2 px)
{
    return t_PrevDepth.Load(int3(px, 0));
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 px)
{
    return t_Depth.Load(int3(px, 0)) > 0.0;
}

float2 FFX_DNSR_Shadows_ReadVelocity(uint2 px)
{
    return float2(0.0, 0.0);
}

void FFX_DNSR_Shadows_WriteMetadata(uint linearTileIndex, uint mask)
{
    uint tilesX = (uint(g_Shadow.texSize.x) + 7) / 8;
    u_TileMeta[uint2(linearTileIndex % tilesX, linearTileIndex / tilesX)] = mask;
}

uint FFX_DNSR_Shadows_ReadRaytracedShadowMask(uint linearTileIndex)
{
    uint tilesX = (uint(g_Shadow.texSize.x) + 7) / 8;
    return t_ShadowMask.Load(int3(int2(linearTileIndex % tilesX, linearTileIndex / tilesX), 0));
}

void FFX_DNSR_Shadows_WriteReprojectionResults(uint2 px, float2 shadowClampedVariance)
{
    u_TemporalOut[px] = shadowClampedVariance;
}

void FFX_DNSR_Shadows_WriteMoments(uint2 px, float4 moments)
{
    moments.z = min(moments.z, 32.0);
    u_Moments[px] = moments;
}

float FFX_DNSR_Shadows_HitsLight(uint2 px)
{
    return t_Vis.Load(int3(px, 0));
}

float4 FFX_DNSR_Shadows_ReadPreviousMomentsBuffer(float2 uv)
{
    int2 lastPixel = int2(g_Shadow.texSize.xy) - 1;
    int2 p = clamp(int2(uv * g_Shadow.texSize.xy), int2(0, 0), lastPixel);
    float4 moments = t_PrevMoments.Load(int3(p, 0));
    moments.yz = max(moments.yz, 0.0);
    return moments;
}

float FFX_DNSR_Shadows_ReadHistory(float2 uv)
{
    return t_PrevAccum.SampleLevel(s_LinearClamp, uv, 0).x;
}

bool FFX_DNSR_Shadows_IsFirstFrame()
{
    return g_Shadow.misc.y > 0.5;
}

#define reprojection_tex t_Reproj

#include "ffx/ffx_denoiser_shadows_tileclassification.hlsl"

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex, uint2 gid : SV_GroupID)
{
    FFX_DNSR_Shadows_TileClassification(groupIndex, gid);
}
