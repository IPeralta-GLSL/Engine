#include "denoise_cb.h"

Texture2D<float> t_Vis : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Normal : register(t2);
Texture2D<float2> t_HistoryVis : register(t3);
Texture2D<float> t_HistoryDepth : register(t4);
Texture2D<float4> t_HistoryNormal : register(t5);

RWTexture2D<float2> u_Out : register(u0);
RWTexture2D<float2> u_HistoryVisOut : register(u1);
RWTexture2D<float> u_HistoryDepthOut : register(u2);
RWTexture2D<float4> u_HistoryNormalOut : register(u3);

ConstantBuffer<DenoiseConstants> g_Denoise : register(b0);

float3 SafeNormalize(float3 value)
{
    float squaredLength = dot(value, value);
    return squaredLength > 0.0 ? value * rsqrt(squaredLength) : float3(0.0, 1.0, 0.0);
}

[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint2 pixel = threadId.xy;

    uint width, height;
    t_Depth.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
        return;

    float depth = t_Depth.Load(int3(pixel, 0));
    float4 normalRaw = t_Normal.Load(int3(pixel, 0));

    u_HistoryDepthOut[pixel] = depth;
    u_HistoryNormalOut[pixel] = normalRaw;

    if (depth <= 0.0)
    {
        u_Out[pixel] = float2(1.0, 0.0);
        u_HistoryVisOut[pixel] = float2(1.0, 0.0);
        return;
    }

    float vis = saturate(t_Vis.Load(int3(pixel, 0)));
    float3 normal = SafeNormalize(normalRaw.xyz);
    float rawVariance = max(vis * (1.0 - vis) * g_Denoise.initialVarianceScale, 0.0);

    int2 lastPixel = int2(width, height) - 1;
    float sum = 0.0;
    float sumSq = 0.0;
    float count = 0.0;

    [unroll]
    for (int ny = -1; ny <= 1; ny++)
    {
        [unroll]
        for (int nx = -1; nx <= 1; nx++)
        {
            int2 sp = clamp(int2(pixel) + int2(nx, ny), int2(0, 0), lastPixel);
            float sd = t_Depth.Load(int3(sp, 0));
            if (sd <= 0.0)
                continue;

            float sv = saturate(t_Vis.Load(int3(sp, 0)));
            sum += sv;
            sumSq += sv * sv;
            count += 1.0;
        }
    }

    float clampMin = 0.0;
    float clampMax = 1.0;

    if (count > 1.5)
    {
        float invCount = 1.0 / count;
        float mean = sum * invCount;
        float sigma = sqrt(max(sumSq * invCount - mean * mean, 0.0));
        clampMin = clamp(mean - g_Denoise.clampSigma * sigma, 0.0, vis);
        clampMax = clamp(mean + g_Denoise.clampSigma * sigma, vis, 1.0);
    }

    float value = vis;
    float variance = rawVariance;

    if (g_Denoise.temporalActive > 0.5)
    {
        float2 clipXY = (float2(pixel) + g_Denoise.view.pixelOffset) * g_Denoise.view.windowToClipScale.xy
                      + g_Denoise.view.windowToClipBias.xy;
        float3 viewPos = float3(clipXY * g_Denoise.clipToView * depth, depth);
        float4 worldPos = mul(float4(viewPos, 1.0), g_Denoise.view.matViewToWorld);
        float4 prevClip = mul(worldPos, g_Denoise.prevViewProj);

        if (prevClip.w > 0.0)
        {
            float2 prevNdc = prevClip.xy / prevClip.w;

            if (all(abs(prevNdc) <= 1.0))
            {
                float2 prevWindow = prevNdc * g_Denoise.view.clipToWindowScale.xy + g_Denoise.view.clipToWindowBias.xy;
                float2 sourcePos = prevWindow - 0.5;
                int2 base = int2(floor(sourcePos));
                float2 fracPos = sourcePos - float2(base);
                float depthTolerance = g_Denoise.temporalDepthTolerance * depth + 1e-4;

                float weightTotal = 0.0;
                float2 history = float2(0.0, 0.0);

                [unroll]
                for (int ty = 0; ty < 2; ty++)
                {
                    [unroll]
                    for (int tx = 0; tx < 2; tx++)
                    {
                        float bilinear = (tx == 0 ? 1.0 - fracPos.x : fracPos.x) * (ty == 0 ? 1.0 - fracPos.y : fracPos.y);
                        if (bilinear <= 1e-6)
                            continue;

                        int2 hp = base + int2(tx, ty);
                        if (any(hp < int2(0, 0)) || any(hp > lastPixel))
                            continue;

                        float hd = t_HistoryDepth.Load(int3(hp, 0));
                        if (hd <= 0.0)
                            continue;

                        float3 hn = SafeNormalize(t_HistoryNormal.Load(int3(hp, 0)).xyz);
                        float depthWeight = exp(-abs(hd - depth) / depthTolerance);
                        float normalWeight = pow(max(dot(normal, hn), 0.0), g_Denoise.normalPower);

                        float weight = bilinear * depthWeight * normalWeight;
                        history += weight * t_HistoryVis.Load(int3(hp, 0));
                        weightTotal += weight;
                    }
                }

                if (weightTotal > g_Denoise.historySupport)
                {
                    history /= weightTotal;

                    float clamped = clamp(history.x, clampMin, clampMax);
                    float clampError = abs(clamped - history.x);
                    float alpha = g_Denoise.blendFactor;
                    float oneMinusAlpha = 1.0 - alpha;

                    value = saturate(clamped * oneMinusAlpha + vis * alpha);
                    variance = oneMinusAlpha * oneMinusAlpha * max(history.y, 0.0) + alpha * alpha * rawVariance;
                    variance = max(variance, clampError * clampError);
                }
            }
        }
    }

    variance = max(variance, 0.0);

    u_Out[pixel] = float2(value, variance);
    u_HistoryVisOut[pixel] = float2(value, variance);
}
