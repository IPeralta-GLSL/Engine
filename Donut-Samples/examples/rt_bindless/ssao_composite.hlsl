RWTexture2D<float4> u_Color : register(u0);
Texture2D<float> t_AO : register(t0);
Texture2D<float> t_Depth : register(t1);

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
    float ao = t_AO.Load(int3(pixel, 0));
    float factor = (depth > 0.0) ? lerp(1.0, ao, strength) : 1.0;
    float4 color = u_Color[pixel];
    u_Color[pixel] = float4(color.rgb * factor, color.a);
}
