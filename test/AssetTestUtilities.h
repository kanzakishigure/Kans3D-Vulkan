#pragma once
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Storage/ProductStore.h"

namespace Kans
{
    // Runtime manager over a published Product set for tests.
    inline Scope<AssetManager> MakeAssetManager(Ref<IAssetProvider>      provider,
                                                Ref<const AssetRegistry> registry,
                                                const std::string&       target = "default")
    {
        AssetManagerSpecification spec;
        spec.Target = target;
        return CreateScope<AssetManager>(std::move(provider), std::move(registry), std::move(spec));
    }

    // Publishes the Products an importer staged for the context's source.
    inline std::variant<Ref<const AssetRegistry>, AssetError> PublishStaged(ProductStore&        store,
                                                                            const ImportContext& context,
                                                                            const ImportResult&  imported,
                                                                            const std::string&   fingerprint,
                                                                            const std::string&   target = "default")
    {
        PublishRequest request;
        request.Source           = context.SourceID;
        request.Target           = target;
        request.Fingerprint      = fingerprint;
        request.StagingDirectory = context.StagingDirectory;
        request.Imported         = imported;
        return store.Publish(request);
    }
} // namespace Kans
