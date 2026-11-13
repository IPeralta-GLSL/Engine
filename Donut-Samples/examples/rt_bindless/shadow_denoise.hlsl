#include "denoise_cb.h"

Texture2D<float2> t_In : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Normal : register(t2);

RWTexture2D<float2> u_Out : register(u0);

ConstantBuffer<DenoiseConstants> g_Denoise : register(b0);

float3 SafeNormalize(float3 value)
{
    float squaredLength = dot(value, value);
    return squaredLength > 0.0 ? value * rsqrt(squaredLength) : float3(0.0, 1.0, 0.0);
}

float LoadEdgeDepth(int2 position, int2 lastPixel, float fallback)
{
    if (any(position < int2(0, 0)) || any(position > lastPixel))
        return fallback;

    float depth = t_Depth.Load(int3(position, 0));
    return depth > 0.0 ? depth : fallback;
}

[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint2 pixel = threadId.xy;

    uint width, height;
    t_Depth.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
        return;

    float2 center = t_In.Load(int3(pixel, 0));
    float centerValue = saturate(center.x);

    float depth0 = t_Depth.Load(int3(pixel, 0));
    if (depth0 <= 0.0)
    {
        u_Out[pixel] = float2(1.0, 0.0);
        return;
    }

    int2 lastPixel = int2(width, height) - 1;
    int2 centerPixel = int2(pixel);
    float3 normal0 = SafeNormalize(t_Normal.Load(int3(pixel, 0)).xyz);

    float dzdx = min(abs(LoadEdgeDepth(centerPixel + int2(1, 0), lastPixel, depth0) - depth0),
                     abs(depth0 - LoadEdgeDepth(centerPixel - int2(1, 0), lastPixel, depth0)));
    float dzdy = min(abs(LoadEdgeDepth(centerPixel + int2(0, 1), lastPixel, depth0) - depth0),
                     abs(depth0 - LoadEdgeDepth(centerPixel - int2(0, 1), lastPixel, depth0)));

    float variance0 = max(center.y, 0.0);
    float sigmaL = max(sqrt(2.0 * variance0) * g_Denoise.luminanceSigma, g_Denoise.minLuminanceSigma);

    int step = max(1, (int)g_Denoise.stepSize);
    float stepF = float(step);
    float invSpatialSigma = 1.0 / (2.0 * g_Denoise.spatialSigma * g_Denoise.spatialSigma);
    float depthFloor = g_Denoise.linearDepthTolerance * depth0 + 1e-4;
    bool narrowKernel = step > 1;

    float valueSum = centerValue;
    float varianceSum = variance0;
    float weightSum = 1.0;

    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            if (narrowKernel && (abs(dx) > 1 || abs(dy) > 1))
                continue;

            int2 tap = centerPixel + int2(dx, dy) * step;
            if (any(tap < int2(0, 0)) || any(tap > lastPixel))
                continue;

            float depth = t_Depth.Load(int3(tap, 0));
            if (depth <= 0.0)
                continue;

            float2 tapValue = t_In.Load(int3(tap, 0));
            float tapScalar = saturate(tapValue.x);
            float3 tapNormal = SafeNormalize(t_Normal.Load(int3(tap, 0)).xyz);

            float squaredDistance = float(dx * dx + dy * dy);
            float depthTolerance = (dzdx * abs(float(dx)) + dzdy * abs(float(dy))) * stepF
                                 * g_Denoise.depthThresholdScale + depthFloor;

            float weight = exp(-squaredDistance * invSpatialSigma)
                         * exp(-abs(depth - depth0) / depthTolerance)
                         * pow(max(dot(normal0, tapNormal), 0.0), g_Denoise.normalPower)
                         * exp(-abs(tapScalar - centerValue) / sigmaL);

            valueSum += weight * tapScalar;
            varianceSum += weight * weight * max(tapValue.y, 0.0);
            weightSum += weight;
        }
    }

    float invWeightSum = 1.0 / weightSum;

    u_Out[pixel] = float2(saturate(valueSum * invWeightSum), varianceSum * invWeightSum * invWeightSum);
}
