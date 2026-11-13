#ifndef DENOISE_CB_H
#define DENOISE_CB_H

#include <donut/shaders/view_cb.h>

struct DenoiseConstants
{
    PlanarViewConstants view;

    float4x4 prevViewProj;

    float2   clipToView;
    float    blendFactor;
    float    clampSigma;

    float    historySupport;
    float    temporalActive;
    float    initialVarianceScale;
    float    temporalDepthTolerance;

    float    depthThresholdScale;
    float    linearDepthTolerance;
    float    normalPower;
    float    luminanceSigma;

    float    spatialSigma;
    float    minLuminanceSigma;
    float    stepSize;
    float    pad0;
};

#endif
