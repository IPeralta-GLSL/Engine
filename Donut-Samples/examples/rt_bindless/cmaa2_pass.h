#ifndef CMAA2_PASS_H
#define CMAA2_PASS_H

#include <nvrhi/nvrhi.h>
#include <donut/engine/ShaderFactory.h>

struct Cmaa2Pass
{
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingSetHandle bindingSet;
    nvrhi::ComputePipelineHandle edgesPipeline;
    nvrhi::ComputePipelineHandle argsPipeline;
    nvrhi::ComputePipelineHandle candidatesPipeline;
    nvrhi::ComputePipelineHandle applyPipeline;

    nvrhi::TextureHandle edges;
    nvrhi::TextureHandle heads;
    nvrhi::TextureHandle output;
    nvrhi::TextureHandle inputColor;
    nvrhi::BufferHandle candidates;
    nvrhi::BufferHandle blendItemList;
    nvrhi::BufferHandle blendLocationList;
    nvrhi::BufferHandle control;
    nvrhi::BufferHandle indirect;

    uint32_t width = 0;
    uint32_t height = 0;
    bool firstRun = true;

    bool Init(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaders, uint32_t w, uint32_t h, nvrhi::ITexture* color)
    {
        width = w;
        height = h;
        firstRun = true;

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5),
            nvrhi::BindingLayoutItem::RawBuffer_UAV(6),
            nvrhi::BindingLayoutItem::RawBuffer_UAV(7)
        };
        layout = device->createBindingLayout(layoutDesc);

        const char* entries[4] = { "EdgesColor2x2CS", "ComputeDispatchArgsCS", "ProcessCandidatesCS", "DeferredColorApply2x2CS" };
        const std::vector<donut::engine::ShaderMacro> defines = {
            { "CMAA2_UAV_STORE_TYPED", "1" },
            { "CMAA2_UAV_STORE_CONVERT_TO_SRGB", "1" },
            { "CMAA2_UAV_STORE_TYPED_UNORM_FLOAT", "1" },
            { "CMAA2_EDGE_DETECTION_LUMA_PATH", "1" }
        };
        nvrhi::ComputePipelineHandle created[4];
        for (int i = 0; i < 4; i++)
        {
            nvrhi::ShaderHandle shader = shaders.CreateShader("app/cmaa2/CMAA2.hlsl", entries[i], &defines, nvrhi::ShaderType::Compute);
            if (!shader)
                return false;
            nvrhi::ComputePipelineDesc desc;
            desc.bindingLayouts = { layout };
            desc.CS = shader;
            created[i] = device->createComputePipeline(desc);
            if (!created[i])
                return false;
        }
        edgesPipeline = created[0];
        argsPipeline = created[1];
        candidatesPipeline = created[2];
        applyPipeline = created[3];

        nvrhi::TextureDesc texDesc;
        texDesc.width = (w + 1) / 2;
        texDesc.height = h;
        texDesc.format = nvrhi::Format::R32_UINT;
        texDesc.isUAV = true;
        texDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        texDesc.keepInitialState = true;
        texDesc.debugName = "Cmaa2Edges";
        edges = device->createTexture(texDesc);

        texDesc.width = (w + 1) / 2;
        texDesc.height = (h + 1) / 2;
        texDesc.format = nvrhi::Format::R32_UINT;
        texDesc.debugName = "Cmaa2Heads";
        heads = device->createTexture(texDesc);

        texDesc.width = w;
        texDesc.height = h;
        texDesc.format = nvrhi::Format::SRGBA8_UNORM;
        texDesc.isTypeless = true;
        texDesc.debugName = "Cmaa2Output";
        output = device->createTexture(texDesc);
        inputColor = color;

        nvrhi::BufferDesc bufDesc;
        bufDesc.canHaveUAVs = true;
        bufDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        bufDesc.keepInitialState = true;

        bufDesc.byteSize = uint64_t(w) * h / 4 * 4;
        bufDesc.structStride = 4;
        bufDesc.debugName = "Cmaa2Candidates";
        candidates = device->createBuffer(bufDesc);

        bufDesc.byteSize = uint64_t(w) * h / 2 * 8;
        bufDesc.structStride = 8;
        bufDesc.debugName = "Cmaa2BlendItemList";
        blendItemList = device->createBuffer(bufDesc);

        bufDesc.byteSize = (uint64_t(w) * h + 3) / 6 * 4;
        bufDesc.structStride = 4;
        bufDesc.debugName = "Cmaa2BlendLocationList";
        blendLocationList = device->createBuffer(bufDesc);

        bufDesc.structStride = 0;
        bufDesc.canHaveRawViews = true;
        bufDesc.byteSize = 16 * 4;
        bufDesc.debugName = "Cmaa2Control";
        control = device->createBuffer(bufDesc);

        bufDesc.byteSize = 4 * 4;
        bufDesc.isDrawIndirectArgs = true;
        bufDesc.debugName = "Cmaa2Indirect";
        indirect = device->createBuffer(bufDesc);

        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, color),
            nvrhi::BindingSetItem::Texture_UAV(0, output, nvrhi::Format::RGBA8_UNORM),
            nvrhi::BindingSetItem::Texture_UAV(1, edges),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(2, candidates),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(3, blendLocationList),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(4, blendItemList),
            nvrhi::BindingSetItem::Texture_UAV(5, heads),
            nvrhi::BindingSetItem::RawBuffer_UAV(6, control),
            nvrhi::BindingSetItem::RawBuffer_UAV(7, indirect)
        };
        bindingSet = device->createBindingSet(setDesc, layout);
        return bindingSet != nullptr;
    }

    nvrhi::ITexture* GetOutput() const { return output; }

    void Execute(nvrhi::ICommandList* cl)
    {
        if (!bindingSet)
            return;

        cl->copyTexture(output, nvrhi::TextureSlice(), inputColor, nvrhi::TextureSlice());

        if (firstRun)
        {
            firstRun = false;
            nvrhi::ComputeState state;
            state.pipeline = argsPipeline;
            state.bindings = { bindingSet };
            cl->setComputeState(state);
            cl->dispatch(2, 1, 1);
        }

        const uint32_t outKernel = 14;
        nvrhi::ComputeState state;
        state.bindings = { bindingSet };

        state.pipeline = edgesPipeline;
        cl->setComputeState(state);
        cl->dispatch((width + outKernel * 2 - 1) / (outKernel * 2), (height + outKernel * 2 - 1) / (outKernel * 2), 1);

        state.pipeline = argsPipeline;
        cl->setComputeState(state);
        cl->dispatch(2, 1, 1);

        state.pipeline = candidatesPipeline;
        state.indirectParams = indirect;
        cl->setComputeState(state);
        cl->dispatchIndirect(0);

        state.pipeline = argsPipeline;
        state.indirectParams = nullptr;
        cl->setComputeState(state);
        cl->dispatch(1, 2, 1);

        state.pipeline = applyPipeline;
        state.indirectParams = indirect;
        cl->setComputeState(state);
        cl->dispatchIndirect(0);
    }
};

#endif
