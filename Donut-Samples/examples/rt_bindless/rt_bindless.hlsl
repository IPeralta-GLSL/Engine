/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma pack_matrix(row_major)

#include <donut/shaders/bindless.h>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/packing.hlsli>
#include <donut/shaders/surface.hlsli>
#include <donut/shaders/lighting.hlsli>
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/sky.hlsli>
#include "lighting_cb.h"

struct RayPayload
{
    float committedRayT;
    uint instanceID;
    uint primitiveIndex;
    uint geometryIndex;
    float2 barycentrics;
};

ConstantBuffer<LightingConstants> g_Const : register(b0);
ConstantBuffer<ProceduralSkyShaderParameters> g_Sky : register(b1);

struct FrameConstants
{
    uint frameIndex;
    uint historyValid;
    float blend;
    float pad;
};
ConstantBuffer<FrameConstants> g_Frame : register(b2);

RWTexture2D<float4> u_Output : register(u0);
RWTexture2D<float4> u_HistoryOut : register(u1);
RWTexture2D<float> u_DepthOut : register(u2);
RWTexture2D<float4> u_NormalOut : register(u3);

RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<InstanceData> t_InstanceData : register(t1);
StructuredBuffer<GeometryData> t_GeometryData : register(t2);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t3);
Texture2D<float4> t_History : register(t4);

SamplerState s_MaterialSampler : register(s0);

VK_BINDING(0, 1) ByteAddressBuffer t_BindlessBuffers[] : register(t0, space1);
VK_BINDING(1, 1) Texture2D t_BindlessTextures[] : register(t0, space2);

RayDesc setupPrimaryRay(uint2 pixelPosition, PlanarViewConstants view)
{
    float2 uv = (float2(pixelPosition) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    worldPos.xyz /= worldPos.w;

    RayDesc ray;
    ray.Origin = view.cameraDirectionOrPosition.xyz;
    ray.Direction = normalize(worldPos.xyz - ray.Origin);
    ray.TMin = 0;
    ray.TMax = 1000;
    return ray;
}

RayDesc setupShadowRay(float3 surfacePos, float3 viewIncident)
{
    RayDesc ray;
    ray.Origin = surfacePos - viewIncident * 0.001;
    ray.Direction = -g_Const.light.direction;
    ray.TMin = 0;
    ray.TMax = 1000;
    return ray;
}

enum GeometryAttributes
{
    GeomAttr_Position   = 0x01,
    GeomAttr_TexCoord   = 0x02,
    GeomAttr_Normal     = 0x04,
    GeomAttr_Tangents   = 0x08,

    GeomAttr_All        = 0x0F
};

struct GeometrySample
{
    InstanceData instance;
    GeometryData geometry;
    MaterialConstants material;

    float3 vertexPositions[3];
    float2 vertexTexcoords[3];

    float3 objectSpacePosition;
    float2 texcoord;
    float3 flatNormal;
    float3 geometryNormal;
    float4 tangent;
};

GeometrySample getGeometryFromHit(
    uint instanceIndex,
    uint triangleIndex,
    uint geometryIndex,
    float2 rayBarycentrics,
    GeometryAttributes attributes,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<MaterialConstants> materialBuffer)
{
    GeometrySample gs = (GeometrySample)0;

    gs.instance = instanceBuffer[instanceIndex];
    gs.geometry = geometryBuffer[gs.instance.firstGeometryIndex + geometryIndex];
    gs.material = materialBuffer[gs.geometry.materialIndex];

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.vertexBufferIndex)];

    float3 barycentrics;
    barycentrics.yz = rayBarycentrics;
    barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);

    uint3 indices = indexBuffer.Load3(gs.geometry.indexOffset + triangleIndex * c_SizeOfTriangleIndices);

    if (attributes & GeomAttr_Position)
    {
        gs.vertexPositions[0] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[0] * c_SizeOfPosition));
        gs.vertexPositions[1] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[1] * c_SizeOfPosition));
        gs.vertexPositions[2] = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices[2] * c_SizeOfPosition));
        gs.objectSpacePosition = interpolate(gs.vertexPositions, barycentrics);
    }

    if ((attributes & GeomAttr_TexCoord) && gs.geometry.texCoord1Offset != ~0u)
    {
        gs.vertexTexcoords[0] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[0] * c_SizeOfTexcoord));
        gs.vertexTexcoords[1] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[1] * c_SizeOfTexcoord));
        gs.vertexTexcoords[2] = asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices[2] * c_SizeOfTexcoord));
        gs.texcoord = interpolate(gs.vertexTexcoords, barycentrics);
    }

    if ((attributes & GeomAttr_Normal) && gs.geometry.normalOffset != ~0u)
    {
        float3 normals[3];
        normals[0] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[0] * c_SizeOfNormal));
        normals[1] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[1] * c_SizeOfNormal));
        normals[2] = Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices[2] * c_SizeOfNormal));
        gs.geometryNormal = interpolate(normals, barycentrics);
        gs.geometryNormal = mul(gs.instance.transform, float4(gs.geometryNormal, 0.0)).xyz;
        gs.geometryNormal = normalize(gs.geometryNormal);
    }

    if ((attributes & GeomAttr_Tangents) && gs.geometry.tangentOffset != ~0u)
    {
        float4 tangents[3];
        tangents[0] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[0] * c_SizeOfNormal));
        tangents[1] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[1] * c_SizeOfNormal));
        tangents[2] = Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices[2] * c_SizeOfNormal));
        gs.tangent.xyz = interpolate(tangents, barycentrics).xyz;
        gs.tangent.xyz = mul(gs.instance.transform, float4(gs.tangent.xyz, 0.0)).xyz;
        gs.tangent.xyz = normalize(gs.tangent.xyz);
        gs.tangent.w = tangents[0].w;
    }

    float3 objectSpaceFlatNormal = normalize(cross(
        gs.vertexPositions[1] - gs.vertexPositions[0],
        gs.vertexPositions[2] - gs.vertexPositions[0]));

    gs.flatNormal = normalize(mul(gs.instance.transform, float4(objectSpaceFlatNormal, 0.0)).xyz);

    return gs;
}

enum MaterialAttributes
{
    MatAttr_BaseColor    = 0x01,
    MatAttr_Emissive     = 0x02,
    MatAttr_Normal       = 0x04,
    MatAttr_MetalRough   = 0x08,
    MatAttr_Transmission = 0x10,

    MatAttr_All          = 0x1F
};

MaterialSample sampleGeometryMaterial(
    GeometrySample gs, 
    float2 texGrad_x, 
    float2 texGrad_y, 
    float mipLevel, // <-- Use a compile time constant for mipLevel, < 0 for aniso filtering
    MaterialAttributes attributes, 
    SamplerState materialSampler)
{
    MaterialTextureSample textures = DefaultMaterialTextures();

    if ((attributes & MatAttr_BaseColor) && (gs.material.baseOrDiffuseTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        Texture2D diffuseTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.baseOrDiffuseTextureIndex)];
        
        if (mipLevel >= 0)
            textures.baseOrDiffuse = diffuseTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.baseOrDiffuse = diffuseTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
    }

    if ((attributes & MatAttr_Emissive) && (gs.material.emissiveTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseEmissiveTexture) != 0)
    {
        Texture2D emissiveTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.emissiveTextureIndex)];
        
        if (mipLevel >= 0)
            textures.emissive = emissiveTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.emissive = emissiveTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
    }
    
    if ((attributes & MatAttr_Normal) && (gs.material.normalTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseNormalTexture) != 0)
    {
        Texture2D normalsTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.normalTextureIndex)];
        
        if (mipLevel >= 0)
            textures.normal = normalsTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.normal = normalsTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
    }

    if ((attributes & MatAttr_MetalRough) && (gs.material.metalRoughOrSpecularTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
    {
        Texture2D specularTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.metalRoughOrSpecularTextureIndex)];

        if (mipLevel >= 0)
            textures.metalRoughOrSpecular = specularTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.metalRoughOrSpecular = specularTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
    }

    if ((attributes & MatAttr_Transmission) && (gs.material.transmissionTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseTransmissionTexture) != 0)
    {
        Texture2D transmissionTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.transmissionTextureIndex)];

        if (mipLevel >= 0)
            textures.transmission = transmissionTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        else
            textures.transmission = transmissionTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
    }

    return EvaluateSceneMaterial(gs.geometryNormal, gs.tangent, gs.material, textures);
}

bool considerTransparentMaterial(uint instanceIndex, uint triangleIndex, uint geometryIndex, float2 rayBarycentrics)
{
    GeometrySample gs = getGeometryFromHit(instanceIndex, triangleIndex, geometryIndex, rayBarycentrics,
        GeomAttr_TexCoord, t_InstanceData, t_GeometryData, t_MaterialConstants);

    if (gs.material.domain == MaterialDomain_AlphaBlended ||
        gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
        return false;

    if (gs.material.domain != MaterialDomain_AlphaTested)
        return true;

    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0, MatAttr_BaseColor | MatAttr_Transmission, s_MaterialSampler);

    bool alphaMask = ms.opacity >= gs.material.alphaCutoff;

    return alphaMask;
}

void traceRay(RayDesc ray, inout RayPayload payload)
{
    payload.instanceID = ~0u;

#if USE_RAY_QUERY

    RayQuery<RAY_FLAG_NONE> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, 0xff, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidateTriangleBarycentrics()))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.geometryIndex = rayQuery.CommittedGeometryIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }

#else // !USE_RAY_QUERY

    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);

#endif
}

float3 SkyColor(float3 rayDirection)
{
    float angularSizeOfPixel = 1.0 / 1024.0;
    return ProceduralSky(g_Sky, normalize(rayDirection), angularSizeOfPixel);
}

float3 traceReflectionRay(float3 worldPos, float3 viewDirection, MaterialSample matSample)
{
    float3 reflectDir = reflect(viewDirection, matSample.shadingNormal);
    RayDesc reflRay;
    reflRay.Origin = worldPos + reflectDir * 0.01;
    reflRay.Direction = reflectDir;
    reflRay.TMin = 0;
    reflRay.TMax = 1000;

    RayPayload reflPayload = (RayPayload)0;
    traceRay(reflRay, reflPayload);

    if (reflPayload.instanceID == ~0u)
    {
        return SkyColor(reflRay.Direction) * matSample.specularF0 * 0.5;
    }

    GeometrySample reflGs = getGeometryFromHit(reflPayload.instanceID, reflPayload.primitiveIndex, reflPayload.geometryIndex, reflPayload.barycentrics,
        GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);

    float3 reflWorldPos = mul(reflGs.instance.transform, float4(reflGs.objectSpacePosition, 1.0)).xyz;
    float3 reflViewDir = normalize(reflWorldPos - worldPos);
    float3 reflDiffuse = 0, reflSpecular = 0;

    RayDesc reflShadowRay = setupShadowRay(reflWorldPos, reflViewDir);
    RayPayload reflShadowPayload = (RayPayload)0;
    traceRay(reflShadowRay, reflShadowPayload);
    if (reflShadowPayload.instanceID == ~0u)
    {
        MaterialSample reflMs = sampleGeometryMaterial(reflGs, 0, 0, -1, MatAttr_All, s_MaterialSampler);
        ShadeSurface(g_Const.light, reflMs, reflWorldPos, reflViewDir, reflDiffuse, reflSpecular);
    }

    return (reflDiffuse + reflSpecular) * matSample.specularF0 * 0.3;
}
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 cosineHemisphere(float2 u, float3 n)
{
    float r = sqrt(u.x);
    float theta = 6.2831853 * u.y;
    float3 up = abs(n.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    float z = sqrt(max(0.0, 1.0 - u.x));
    return normalize(t * (r * cos(theta)) + b * (r * sin(theta)) + n * z);
}

float3 SkyAmbientColor(float3 direction)
{
    float3 d = normalize(direction);
    float elevation = asin(clamp(dot(d, g_Sky.directionUp), -1.0, 1.0));
    float top = smoothstep(0.0, g_Sky.horizonSize, elevation);
    float bottom = smoothstep(0.0, g_Sky.horizonSize, -elevation);
    return lerp(lerp(g_Sky.horizonColor, g_Sky.groundColor, bottom), g_Sky.skyColor, top);
}

float computeSunVisibility(float3 worldPos, float3 n, uint2 pixelPosition)
{
    float3 L = normalize(-g_Const.light.direction);
    float3 up = abs(L.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t = normalize(cross(up, L));
    float3 b = cross(L, t);

    const int numSamples = 2;
    float cone = 0.03;
    float seed = hash12(float2(pixelPosition) + float(g_Frame.frameIndex) * 13.0);
    float lit = 0;
    for (int i = 0; i < numSamples; i++)
    {
        float fi = float(i);
        float angle = fi * 2.39996323 + seed * 6.2831853;
        float radius = sqrt((fi + 0.5) / float(numSamples)) * cone;
        float3 dir = normalize(L + (t * cos(angle) + b * sin(angle)) * radius);

        RayDesc ray;
        ray.Origin = worldPos + n * 0.002;
        ray.Direction = dir;
        ray.TMin = 0;
        ray.TMax = 1000;

        RayPayload p = (RayPayload)0;
        traceRay(ray, p);
        if (p.instanceID == ~0u)
            lit += 1.0;
    }
    return lit / float(numSamples);
}

float computeAO(float3 worldPos, float3 n, uint2 pixelPosition)
{
    const int numSamples = 2;
    float radius = 1.0;
    float seed = hash12(float2(pixelPosition) + 17.0 + float(g_Frame.frameIndex) * 71.0);
    float occlusion = 0;
    for (int i = 0; i < numSamples; i++)
    {
        float fi = float(i);
        float2 u = float2(frac(fi * 0.61803398875 + seed), frac(fi * 0.75487766624 + seed * 1.7));
        float3 dir = cosineHemisphere(u, n);

        RayDesc ray;
        ray.Origin = worldPos + n * 0.01;
        ray.Direction = dir;
        ray.TMin = 0;
        ray.TMax = radius;

        RayPayload p = (RayPayload)0;
        traceRay(ray, p);
        if (p.instanceID != ~0u)
            occlusion += 1.0;
    }
    return 1.0 - (occlusion / float(numSamples)) * 0.85;
}

    float3 shadeSurface(
    uint2 pixelPosition,
    RayPayload payload,
    float3 viewDirection)
{
    GeometrySample gs = getGeometryFromHit(payload.instanceID, payload.primitiveIndex, payload.geometryIndex, payload.barycentrics, 
        GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    RayDesc ray_0 = setupPrimaryRay(pixelPosition, g_Const.view);
    RayDesc ray_x = setupPrimaryRay(pixelPosition + uint2(1, 0), g_Const.view);
    RayDesc ray_y = setupPrimaryRay(pixelPosition + uint2(0, 1), g_Const.view);
    float3 worldSpacePositions[3];
    worldSpacePositions[0] = mul(gs.instance.transform, float4(gs.vertexPositions[0], 1.0)).xyz;
    worldSpacePositions[1] = mul(gs.instance.transform, float4(gs.vertexPositions[1], 1.0)).xyz;
    worldSpacePositions[2] = mul(gs.instance.transform, float4(gs.vertexPositions[2], 1.0)).xyz;
    float3 bary_0 = computeRayIntersectionBarycentrics(worldSpacePositions, ray_0.Origin, ray_0.Direction);
    float3 bary_x = computeRayIntersectionBarycentrics(worldSpacePositions, ray_x.Origin, ray_x.Direction);
    float3 bary_y = computeRayIntersectionBarycentrics(worldSpacePositions, ray_y.Origin, ray_y.Direction);
    float2 texcoord_0 = interpolate(gs.vertexTexcoords, bary_0);
    float2 texcoord_x = interpolate(gs.vertexTexcoords, bary_x);
    float2 texcoord_y = interpolate(gs.vertexTexcoords, bary_y);
    float2 texGrad_x = texcoord_x - texcoord_0;
    float2 texGrad_y = texcoord_y - texcoord_0;

    MaterialSample ms = sampleGeometryMaterial(gs, texGrad_x, texGrad_y, -1, MatAttr_All, s_MaterialSampler);

    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, viewDirection);

    float3 worldPos = mul(gs.instance.transform, float4(gs.objectSpacePosition, 1.0)).xyz;

    float viewZ = mul(float4(worldPos, 1.0), g_Const.view.matWorldToView).z;
    u_DepthOut[pixelPosition] = max(viewZ, 0.0);
    u_NormalOut[pixelPosition] = float4(ms.shadingNormal, 1.0);

    float rawVisibility = computeSunVisibility(worldPos, ms.shadingNormal, pixelPosition);
    float rawAO = 1.0;

    float2 accum = float2(rawAO, rawVisibility);
    if (g_Frame.historyValid != 0)
    {
        float2 history = t_History.Load(int3(int2(pixelPosition), 0)).xy;
        accum = lerp(history, accum, g_Frame.blend);
    }
    u_HistoryOut[pixelPosition] = float4(accum, 0.0, 0.0);

    float visibility = accum.y;
    float ao = accum.x;

    float3 diffuseTerm = 0, specularTerm = 0;
    if (visibility > 0.0)
    {
        ShadeSurface(g_Const.light, ms, worldPos, viewDirection, diffuseTerm, specularTerm);
        diffuseTerm *= visibility;
        specularTerm *= visibility;
    }

    float3 ambient = ms.diffuseAlbedo * SkyAmbientColor(ms.shadingNormal) * ao * 1.5;

    float3 reflection = traceReflectionRay(worldPos, viewDirection, ms);

    return diffuseTerm + specularTerm + ambient + ms.emissiveColor * 1.2 + reflection;
}

#if !USE_RAY_QUERY
struct Attributes
{
    float2 uv;
};

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.committedRayT = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;
}


[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    if (!considerTransparentMaterial(InstanceID(), PrimitiveIndex(), GeometryIndex(), attrib.uv))
        IgnoreHit();
}
#endif

#if USE_RAY_QUERY
[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 pixelPosition = DispatchRaysIndex().xy;
#endif

    RayDesc ray = setupPrimaryRay(pixelPosition, g_Const.view);

    RayPayload payload = (RayPayload)0;
    traceRay(ray, payload);

    if (payload.instanceID == ~0u)
    {
        u_Output[pixelPosition] = float4(SkyColor(ray.Direction), 1.0);
        u_DepthOut[pixelPosition] = 0.0;
        u_NormalOut[pixelPosition] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    u_Output[pixelPosition] = float4(shadeSurface(pixelPosition, payload, ray.Direction), 0);
}
