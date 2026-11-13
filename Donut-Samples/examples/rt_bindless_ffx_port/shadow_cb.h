#ifndef SHADOW_CB_H
#define SHADOW_CB_H

#include <donut/shaders/view_cb.h>

struct ShadowDenoiseConstants
{
    float4x4 matClipToView;
    float4x4 matViewToWorld;
    float4x4 matClipToWorld;
    float4x4 prevViewProj;
    float4x4 prevClipToView;

    float4   texSize;
    float4   clipToView;
    float4   misc;
    float4   filter;
};

#endif
