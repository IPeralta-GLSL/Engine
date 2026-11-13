RWTexture2D<float4> u_Color : register(u0);
Texture2D<float> t_AO : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_Direct : register(t2);
Texture2D<float> t_Vis : register(t3);
Texture2D<float> t_DenVis : register(t4);

cbuffer CompositeConstants : register(b0)
{
    float strength;
    float pad0;
    float pad1;
    float pad2;
};

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    float depth = t_Depth.Load(int3(pixel, 0));
    float4 color = u_Color[pixel];

    if (depth > 0.0)
    {
        float vis = t_Vis.Load(int3(pixel, 0));
        float denVis = t_DenVis.Load(int3(pixel, 0));
        float3 direct = t_Direct.Load(int3(pixel, 0)).xyz;

        color.rgb += direct * (denVis - vis);

        float ao = t_AO.Load(int3(pixel, 0));
        color.rgb *= lerp(1.0, ao, strength);
    }

    u_Color[pixel] = color;
}
