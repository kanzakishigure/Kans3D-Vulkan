#pragma once
#include "RenderView.h"
#include "RenderWorld.h"
#include "Renderer.h"
#include "ViewTarget.h"

namespace Kans
{
    // One synchronous pipeline invocation. No viewport configuration or persistent scene ownership.
    struct RenderFrame
    {
        RenderWorld&                  World;
        RenderView&                   View;
        ViewTarget&                   Output;
        Renderer&                     Services;
        SceneRenderStats&             Stats;
        SpriteStatistics&             SpriteStats;
        bool&                         Complete;
        bool                          DeferToneMapping = false, PostProcessHDR = false;
        const GpuMesh*                GetMesh(const MeshKey& id) const;
        const GpuMaterialBindings*    GetBindings(AssetID id, bool debug) const;
        const GpuShader*              GetShader(AssetID material, AssetID shader) const;
        RenderAssetCache*             Assets() const { return View.Assets.get(); }
        RenderHardware::RenderingDesc SceneTarget(RenderHardware::LoadOp load, bool hdr = false) const
        {
            return Output.Target(load, hdr);
        }
        std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> GetMaterialEnvironment();
        std::shared_ptr<RenderHardware::Device> GetDevice() const { return Services.GetDevice(); }
        void                                    PrepareDrawLists();
        bool                                    HasMaterialPass(const RenderView& view, const std::string& tag);
        void                                    DrawShaderMaterials(
            RenderHardware::CommandList&                                                       commands,
            const std::function<void(const RenderHardware::Handle<RenderHardware::Texture>&)>& sampled   = {},
            const std::string&                                                                 tag       = {},
            const RenderView*                                                                  view      = nullptr,
            RenderHardware::Handle<RenderHardware::CommandList>*                               finalPost = nullptr);
        void SubmitPrefabPreview(const PrefabPreview& preview);
        void AppendPrefabPreviewDraws(const PrefabPreview& preview, std::vector<MeshDraw>& draws);
        void AppendEntityDraws(UUID entity, uint32_t id, std::vector<MeshDraw>& draws);
        void DrawModelGeometry(const ViewGeometryDraw& draw, RenderHardware::CommandList& commands);
        void DrawGrid(const ViewGrid& grid, RenderHardware::CommandList& commands);
        void DrawSkybox(const ViewSkybox& sky, RenderHardware::CommandList& commands);
    };
} // namespace Kans
