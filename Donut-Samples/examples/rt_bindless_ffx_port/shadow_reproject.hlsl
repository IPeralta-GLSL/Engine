#include "shadow_cb.h"

Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normal : register(t1);
Texture2D<float> t_PrevDepth : register(t2);

RWTexture2D<float4> u_Reproj : register(u0);
RWTexture2D<float> u_DepthHistory : register(u1);

ConstantBuffer<ShadowDenoiseConstants> g_Shadow : register(b0);

[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint2 pixel = threadId.xy;

    float2 size = g_Shadow.texSize.xy;
    float2 invSize = g_Shadow.texSize.zw;
    int2 lastPixel = int2(size) - 1;

    if (any(int2(pixel) > lastPixel))
        return;

    float depth = t_Depth.Load(int3(pixel, 0));
    u_DepthHistory[pixel] = depth;

    float2 uv = (float2(pixel) + 0.5) * invSize;

    if (depth <= 0.0)
    {
        u_Reproj[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 viewPos = float3(ndc * g_Shadow.clipToView.xy * depth, depth);
    float4 worldPos = mul(float4(viewPos, 1.0), g_Shadow.matViewToWorld);
    float4 prevClip = mul(worldPos, g_Shadow.prevViewProj);

    if (prevClip.w <= 0.0)
    {
        u_Reproj[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float2 prevNdc = prevClip.xy / prevClip.w;
    float2 prevUV = prevNdc * float2(0.5, -0.5) + 0.5;
    float2 uvDiff = prevUV - uv;

    uvDiff = floor(uvDiff * 32767.0 + 0.5) / 32767.0;
    prevUV = uv + uvDiff;

    float4 prevViewPos = mul(float4(prevNdc, prevClip.z / prevClip.w, 1.0), g_Shadow.prevClipToView);
    prevViewPos.xyz /= prevViewPos.w;

    float3 normalVS = t_Normal.Load(int3(pixel, 0)).xyz;
    float normalLen = length(normalVS);
    if (normalLen <= 0.0)
    {
        u_Reproj[pixel] = float4(uvDiff, 0.0, 0.0);
        return;
    }
    normalVS /= normalLen;

    float planeDistPrev = dot(normalVS, prevViewPos.xyz);
    float planeDistPrevDz = min(-0.2, normalVS.z);

    int2 base = int2(floor(prevUV * size - 0.5));
    float acceptance = g_Shadow.misc.w * depth / max(-dot(normalVS, normalize(viewPos)), 1e-3);

    float4 quadValidity = float4(0.0, 0.0, 0.0, 0.0);

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        int2 offset = int2(i & 1, i >> 1);
        int2 tap = base + offset;

        if (any(tap < int2(0, 0)) || any(tap > lastPixel))
            continue;

        float prevDepth = t_PrevDepth.Load(int3(tap, 0));
        if (prevDepth <= 0.0)
            continue;

        float quadDist = abs(planeDistPrevDz * (prevDepth - prevViewPos.z));
        quadValidity[i] = quadDist <= acceptance ? 1.0 : 0.0;
    }

    float validity = dot(quadValidity, float4(1.0, 2.0, 4.0, 8.0)) / 15.0;

    if (any(saturate(prevUV) != prevUV))
        validity = 0.0;

    if (g_Shadow.misc.y > 0.5)
        validity = 0.0;

    u_Reproj[pixel] = float4(uvDiff, validity, 0.0);
}
