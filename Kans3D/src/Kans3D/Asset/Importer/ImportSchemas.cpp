#include "kspch.h"
#include "AudioImportSettings.h"
#include "AudioImporter.h"
#include "ModelImporter.h"
#include "TextureImportSettings.h"
#include "TextureImporter.h"

namespace Kans
{
    namespace
    {
        using T = ImportSettingDesc::Type;
        ImportSettingDesc Setting(std::string                        key,
                                  T                                  kind,
                                  std::string                        value,
                                  std::string                        label,
                                  std::string                        group,
                                  std::string                        description,
                                  std::initializer_list<const char*> choices = {})
        {
            ImportSettingDesc s;
            s.Key         = std::move(key);
            s.Kind        = kind;
            s.Default     = std::move(value);
            s.Label       = std::move(label);
            s.Group       = std::move(group);
            s.Description = std::move(description);
            s.KeyPattern  = s.Key.find('<') != std::string::npos;
            for (auto choice : choices)
                s.Options.push_back({choice, choice, {}});
            return s;
        }
    } // namespace
    ImportSettingsSchema ModelImporter::GetSettingsSchema() const
    {
        ImportSettingsSchema schema;
        auto&                s = schema.Settings;
        s.push_back(Setting("scale", T::Float, "1", "Scale", "Mesh", "Positive uniform model scale."));
        s.back().Min  = 0;
        s.back().Step = .1;
        s.push_back(Setting("mesh.smooth_normals",
                            T::Enum,
                            "false",
                            "Smooth normals",
                            "Mesh",
                            "Weld by position or preserve source control point identity.",
                            {"false", "weld", "source", "true"}));
        s.push_back(Setting("morph.recompute_normals",
                            T::Enum,
                            "auto",
                            "Recompute normals",
                            "Morph targets",
                            "Auto recomputes missing normals and unchanged FBX normals.",
                            {"auto", "true", "false"}));
        for (auto category : {"meshes", "animations", "materials", "skeleton", "morph_targets"})
            s.push_back(Setting(std::string("include.") + category,
                                T::Bool,
                                "true",
                                category,
                                "Contents",
                                std::string("Include ") + category +
                                    ". Disabling skeleton imports meshes as static geometry."));
        s.push_back(Setting("include.mesh.<name>",
                            T::Bool,
                            "true",
                            "Include mesh",
                            "Contents",
                            "Include all source sections with this exact mesh name."));
        s.push_back(Setting("include.clip.<name>",
                            T::Bool,
                            "true",
                            "Include clip",
                            "Contents",
                            "Include all clips with this exact source name."));
        s.push_back(Setting("animation.compression",
                            T::Enum,
                            "off",
                            "Compression",
                            "Animation",
                            "Clip compression mode.",
                            {"off", "lossless", "optimal", "custom"}));
        s.push_back(Setting("animation.compression.<clip-key>",
                            T::Enum,
                            "inherit",
                            "Clip compression",
                            "Animation",
                            "Override compression for a clip object key.",
                            {"inherit", "off", "lossless", "optimal", "custom"}));
        s.back().InheritFrom   = "animation.compression";
        const char* fields[]   = {"position", "shell", "morph"};
        const char* defaults[] = {"0.0001", "0.03", "0.001"};
        for (size_t i = 0; i < 3; ++i)
        {
            s.push_back(Setting(std::string("animation.error.") + fields[i],
                                T::Float,
                                defaults[i],
                                fields[i],
                                "Animation",
                                "Nonnegative tolerance used by custom compression."));
            s.back().Min      = 0;
            s.back().Advanced = true;
            s.push_back(Setting(std::string("animation.error.") + fields[i] + ".<clip-key>",
                                T::Float,
                                "inherit",
                                fields[i],
                                "Animation",
                                "Per-clip custom tolerance; inherit uses the global value."));
            s.back().Min         = 0;
            s.back().Advanced    = true;
            s.back().InheritFrom = std::string("animation.error.") + fields[i];
        }
        s.push_back(Setting("animation.strip_prefix",
                            T::String,
                            "",
                            "Strip prefix",
                            "Animation",
                            "Remove this prefix from joint and shape names."));
        s.push_back(Setting("animation.map.<name>",
                            T::String,
                            "",
                            "Name mapping",
                            "Animation",
                            "Map a source joint/shape name; empty removes the override."));
        s.push_back(Setting("material.<slot-key>",
                            T::Asset,
                            "",
                            "Material remap",
                            "Materials",
                            "Published material ID; empty uses the source material."));
        s.back().Key       = "material.material:<slot>"; // Includes the complete slot key, including its length prefix.
        s.back().AssetKind = AssetType::Material;
        schema.Presets     = {{"Static mesh",
                               "Geometry and materials without animations.",
                               {{"include.meshes", "true"},
                                {"include.materials", "true"},
                                {"include.animations", "false"},
                                {"include.skeleton", "false"},
                                {"include.morph_targets", "true"}}},
                              {"Character (skinned + animations)",
                               "Skinned meshes, morphs and clips.",
                               {{"include.meshes", "true"},
                                {"include.animations", "true"},
                                {"include.materials", "true"},
                                {"include.skeleton", "true"},
                                {"include.morph_targets", "true"}}},
                              {"Animations only",
                               "Skeleton and clips without geometry or materials.",
                               {{"include.meshes", "false"},
                                {"include.materials", "false"},
                                {"include.animations", "true"},
                                {"include.skeleton", "true"}}}};
        return schema;
    }
    ImportSettingsSchema TextureImporter::GetSettingsSchema() const
    {
        ImportSettingsSchema schema;
        auto&                s = schema.Settings;
        s.push_back(Setting("usage",
                            T::Enum,
                            "color",
                            "Usage",
                            "Texture",
                            "Interpret pixels as color, data or tangent-space normals.",
                            {"color", "normal", "data"}));
        s.push_back(Setting("color_space",
                            T::Enum,
                            "auto",
                            "Color space",
                            "Texture",
                            "Auto respects source metadata and usage; HDR is linear.",
                            {"auto", "srgb", "linear"}));
        s.push_back(Setting("alpha_mode",
                            T::Enum,
                            "auto",
                            "Alpha mode",
                            "Texture",
                            "Auto uses channel presence.",
                            {"auto", "opaque", "straight", "premultiplied"}));
        s.push_back(Setting(
            "generate_mips", T::Bool, "true", "Generate mips", "Texture", "Build a complete filtered mip chain."));
        s.push_back(Setting("flip_y", T::Bool, "false", "Flip image Y", "Texture", "Flip source rows."));
        s.push_back(Setting("normal_flip_y",
                            T::Bool,
                            "false",
                            "Flip normal Y",
                            "Texture",
                            "Invert green channel; requires normal usage."));
        s.push_back(Setting("max_size",
                            T::Int,
                            "0",
                            "Maximum size",
                            "Texture",
                            "Limit largest dimension, preserving aspect; 0 means unlimited.",
                            {"0", "256", "512", "1024", "2048", "4096", "8192"}));
        s.back().Min  = 0;
        s.back().Max  = 16384;
        s.back().Step = 1;
        s.push_back(Setting("filter",
                            T::Enum,
                            "linear",
                            "Filter",
                            "Texture",
                            "Minification, magnification and mip filtering.",
                            {"linear", "nearest"}));
        s.push_back(Setting("wrap",
                            T::Enum,
                            "repeat",
                            "Wrap",
                            "Texture",
                            "UV addressing on every axis.",
                            {"repeat", "clamp", "mirror"}));
        schema.Presets = {
            {"Color (sRGB)",
             "Color textures.",
             {{"usage", "color"}, {"color_space", "srgb"}, {"normal_flip_y", "false"}, {"generate_mips", "true"}}},
            {"Normal map",
             "Linear tangent-space normals.",
             {{"usage", "normal"}, {"color_space", "linear"}, {"alpha_mode", "opaque"}, {"generate_mips", "true"}}},
            {"Data / mask (linear)",
             "Uncorrected data channels.",
             {{"usage", "data"}, {"color_space", "linear"}, {"alpha_mode", "opaque"}, {"normal_flip_y", "false"}}},
            {"UI / sprite (no mips)",
             "Color sprites without mipmaps.",
             {{"usage", "color"},
              {"color_space", "srgb"},
              {"generate_mips", "false"},
              {"wrap", "clamp"},
              {"normal_flip_y", "false"}}}};
        return schema;
    }
    ImportSettingsSchema AudioImporter::GetSettingsSchema() const
    {
        ImportSettingsSchema schema;
        schema.Settings = {
            Setting("load_mode",
                    T::Enum,
                    "auto",
                    "Load mode",
                    "Audio",
                    "Auto chooses by decoded duration/size.",
                    {"auto", "decompress", "stream"}),
            Setting("volume", T::Float, "1", "Volume", "Audio", "Default playback volume."),
            Setting("loop_begin", T::Float, "0", "Loop begin", "Audio", "Loop start in seconds."),
            Setting("loop_end", T::Float, "0", "Loop end", "Audio", "Loop end in seconds; 0 uses duration.")};
        for (size_t i = 1; i < schema.Settings.size(); ++i)
            schema.Settings[i].Min = 0;
        schema.Settings[1].Max = 4;
        return schema;
    }
    std::variant<std::monostate, AssetError>
    AudioImporter::ValidateSettings(const std::map<std::string, std::string>& s) const
    {
        auto parsed = ParseAudioImportSettings(s);
        if (auto* error = std::get_if<AssetError>(&parsed))
            return *error;
        return std::monostate {};
    }
    std::variant<std::monostate, AssetError>
    TextureImporter::ValidateSettings(const std::map<std::string, std::string>& s) const
    {
        auto parsed = ParseTextureImportSettings(s);
        if (auto* error = std::get_if<AssetError>(&parsed))
            return *error;
        return std::monostate {};
    }
} // namespace Kans
