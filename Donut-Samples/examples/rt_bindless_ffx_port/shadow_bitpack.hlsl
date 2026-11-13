#include "shadow_cb.h"

Texture2D<float> t_Vis : register(t0);
RWTexture2D<uint> u_ShadowMask : register(u0);

ConstantBuffer<ShadowDenoiseConstants> g_Shadow : register(b0);

uint2 FFX_DNSR_Shadows_GetBufferDimensions()
{
    return uint2(g_Shadow.texSize.xy);
}

bool FFX_DNSR_Shadows_HitsLight(uint2 did, uint2 gtid, uint2 gid)
{
    int2 lastPixel = int2(g_Shadow.texSize.xy) - 1;
    if (any(int2(did) > lastPixel))
        return false;

    return t_Vis.Load(int3(did, 0)) > 0.5;
}

void FFX_DNSR_Shadows_WriteMask(uint linearTileIndex, uint value)
{
    uint tilesX = (uint(g_Shadow.texSize.x) + 7) / 8;
    u_ShadowMask[uint2(linearTileIndex % tilesX, linearTileIndex / tilesX)] = value;
}

#include "ffx/ffx_denoiser_shadows_prepare.hlsl"

[numthreads(8, 4, 1)]
void main(uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
    FFX_DNSR_Shadows_PrepareShadowMask(gtid, gid);
}
