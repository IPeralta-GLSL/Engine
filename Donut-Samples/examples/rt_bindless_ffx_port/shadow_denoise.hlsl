#include "shadow_cb.h"

#define float16_t2 float2
#define float16_t3 float3

Texture2D<float4> t_In : register(t0);
Texture2D<uint> t_TileMeta : register(t1);
Texture2D<float4> t_Normal : register(t2);
Texture2D<float> t_Depth : register(t3);

RWTexture2D<float2> u_Out : register(u0);

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

float FFX_DNSR_Shadows_GetDepthSimilaritySigma()
{
    return g_Shadow.misc.z;
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 px)
{
    return t_Depth.Load(int3(px, 0)) > 0.0;
}

float3 FFX_DNSR_Shadows_ReadNormals(uint2 px)
{
    return t_Normal.Load(int3(px, 0)).xyz;
}

float FFX_DNSR_Shadows_ReadDepth(uint2 px)
{
    return t_Depth.Load(int3(px, 0));
}

float2 FFX_DNSR_Shadows_ReadInput(uint2 px)
{
    return t_In.Load(int3(px, 0)).xy;
}

uint FFX_DNSR_Shadows_ReadTileMetaData(uint linearTileIndex)
{
    uint tilesX = (uint(g_Shadow.texSize.x) + 7) / 8;
    return t_TileMeta.Load(int3(int2(linearTileIndex % tilesX, linearTileIndex / tilesX), 0));
}

#include "ffx/ffx_denoiser_shadows_filter.hlsl"

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
    uint width, height;
    t_Depth.GetDimensions(width, height);

    bool writeResults = true;
    float2 output = FFX_DNSR_Shadows_FilterSoftShadowsPass(gid, gtid, pixel, writeResults, 0, uint(g_Shadow.filter.x));

    if (writeResults && pixel.x < width && pixel.y < height)
        u_Out[pixel] = max(0.0, output);
}
