#include "kspch.h"
#include "ScriptBuild.h"
#include <regex>
#if defined(_WIN32)
#include <Windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#include <cerrno>
#include <sys/wait.h>
extern char** environ;
#endif
#ifndef KANS_DOTNET_EXECUTABLE
#define KANS_DOTNET_EXECUTABLE "dotnet"
#endif

namespace Kans
{
#if defined(_WIN32)
    namespace
    {
        // CommandLineToArgvW/CRT escaping, including quotes and trailing backslashes.
        std::wstring Quote(const std::wstring& argument)
        {
            std::wstring result  = L"\"";
            size_t       slashes = 0;
            for (wchar_t ch : argument)
            {
                if (ch == L'\\')
                {
                    ++slashes;
                    continue;
                }
                result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
                slashes = 0;
                if (ch == L'"')
                    result += L'\\';
                result += ch;
            }
            result.append(slashes * 2, L'\\');
            return result + L'"';
        }
    } // namespace
#endif
    ScriptBuild::Result ScriptBuild::Build(const std::filesystem::path& csproj, Config config)
    {
        Result                   result;
        std::vector<std::string> args {KANS_DOTNET_EXECUTABLE,
                                       "build",
                                       std::filesystem::absolute(csproj).u8string(),
                                       "-c",
                                       config == Config::Debug ? "Debug" : "Release",
                                       "-m:1",
                                       "-nr:false",
                                       "--nologo",
                                       "-p:UseSharedCompilation=false"};
#if defined(_WIN32)
        SECURITY_ATTRIBUTES security {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE              read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &security, 0))
            return result;
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        std::wstring command;
        for (const auto& argument : args)
        {
            if (!command.empty())
                command += L' ';
            command += Quote(std::filesystem::u8path(argument).wstring());
        }
        STARTUPINFOW startup {};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdOutput = write;
        startup.hStdError  = write;
        startup.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process {};
        auto                executable = std::filesystem::u8path(args[0]).wstring();
        bool                started    = CreateProcessW(executable.c_str(),
                                                        command.data(),
                                                        nullptr,
                                                        nullptr,
                                                        TRUE,
                                                        CREATE_NO_WINDOW,
                                                        nullptr,
                                                        nullptr,
                                                        &startup,
                                                        &process);
        CloseHandle(write);
        if (!started)
        {
            CloseHandle(read);
            result.Output = "CreateProcessW failed: " + std::to_string(GetLastError());
            return result;
        }
        char  buffer[4096];
        DWORD count;
        while (ReadFile(read, buffer, sizeof(buffer), &count, nullptr) && count)
            result.Output.append(buffer, count);
        CloseHandle(read);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code;
        GetExitCodeProcess(process.hProcess, &code);
        result.ExitCode = int(code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        int pipeFD[2];
        if (pipe(pipeFD) != 0)
            return result;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFD[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipeFD[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFD[0]);
        posix_spawn_file_actions_addclose(&actions, pipeFD[1]);
        std::vector<char*> argv;
        for (auto& arg : args)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        pid_t process;
        int   status = posix_spawnp(&process, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFD[1]);
        if (status != 0)
        {
            close(pipeFD[0]);
            result.Output = std::strerror(status);
            return result;
        }
        char buffer[4096];
        for (;;)
        {
            auto count = read(pipeFD[0], buffer, sizeof(buffer));
            if (count > 0)
                result.Output.append(buffer, size_t(count));
            else if (count < 0 && errno == EINTR)
                continue;
            else
                break;
        }
        close(pipeFD[0]);
        int code;
        while (waitpid(process, &code, 0) < 0)
        {
            if (errno != EINTR)
                return result;
        }
        result.ExitCode = WIFEXITED(code) ? WEXITSTATUS(code) : -1;
#endif
        result.Success = result.ExitCode == 0;
        const std::regex   pattern(R"(^(.+)\((\d+),(\d+)(?:,\d+,\d+)?\): (error|warning) ([A-Z]+\d+): (.*)$)");
        std::istringstream lines(result.Output);
        std::string        line;
        std::unordered_set<std::string> seen;
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (!std::regex_match(line, match, pattern) || !seen.insert(line).second)
                continue;
            result.Diagnostics.push_back({std::filesystem::u8path(match[1].str()),
                                          std::stoi(match[2]),
                                          std::stoi(match[3]),
                                          match[4],
                                          match[5],
                                          match[6]});
        }
        return result;
    }
} // namespace Kans
