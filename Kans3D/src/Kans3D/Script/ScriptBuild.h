#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Kans
{
    class ScriptBuild
    {
    public:
        enum class Config
        {
            Debug,
            Release
        };
        struct Diagnostic
        {
            std::filesystem::path File;
            int                   Line = 0, Column = 0;
            std::string           Severity, Code, Message;
        };
        struct Result
        {
            bool                    Success  = false;
            int                     ExitCode = -1;
            std::string             Output;
            std::vector<Diagnostic> Diagnostics;
        };
        // Executes an argument vector directly, with combined stdout/stderr capture.
        static Result Build(const std::filesystem::path& csproj, Config config);
    };
} // namespace Kans
