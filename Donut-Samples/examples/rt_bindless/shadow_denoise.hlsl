Texture2D<float> t_Vis : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Normal : register(t2);
RWTexture2D<float> u_Out : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint width, height;
    t_Vis.GetDimensions(width, height);

    float depth0 = t_Depth.Load(int3(pixel, 0));
    if (depth0 <= 0.0)
    {
        u_Out[pixel] = 1.0;
        return;
    }

    float3 normal0 = t_Normal.Load(int3(pixel, 0)).xyz;
    float vis0 = t_Vis.Load(int3(pixel, 0));

    float sum = vis0;
    float weightSum = 1.0;

    const int radius = 2;
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            int2 p = clamp(int2(pixel) + int2(dx, dy), int2(0, 0), int2(int(width) - 1, int(height) - 1));

            float depth = t_Depth.Load(int3(p, 0));
            if (depth <= 0.0)
                continue;

            float3 normal = t_Normal.Load(int3(p, 0)).xyz;
            float vis = t_Vis.Load(int3(p, 0));

            float depthWeight = exp(-abs(depth - depth0) / (0.05 * depth0 + 0.01));
            float normalWeight = pow(max(dot(normal0, normal), 0.0), 8.0);
            float weight = depthWeight * normalWeight;

            sum += vis * weight;
            weightSum += weight;
        }
    }

    u_Out[pixel] = saturate(sum / weightSum);
}
