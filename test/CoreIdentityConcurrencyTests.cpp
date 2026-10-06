#include <gtest/gtest.h>

#include "Kans3D/Core/Hash.h"
#include "Kans3D/Core/UUID.h"

#include <array>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr size_t WorkerCount = 8;

    template<typename Function>
    void RunTogether(Function function)
    {
        std::mutex mutex;
        std::condition_variable changed;
        size_t arrived = 0;
        bool start = false;
        std::array<std::thread, WorkerCount> workers;
        for (size_t i = 0; i < workers.size(); ++i)
            workers[i] = std::thread([&, i] {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ++arrived;
                    changed.notify_all();
                    changed.wait(lock, [&] { return start; });
                }
                function(i);
            });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return arrived == WorkerCount; });
            start = true;
        }
        changed.notify_all();
        for (auto& worker : workers)
            worker.join();
    }

    TEST(CoreIdentityConcurrency, ConcurrentFirstUseAndSubsequentDrawsRemainValid)
    {
        constexpr size_t Draws = 2048;
        std::array<std::vector<uint64_t>, WorkerCount> identities;
        std::array<bool, WorkerCount> valid32 {};
        RunTogether([&](size_t worker) {
            auto& values = identities[worker];
            values.reserve(Draws);
            valid32[worker] = true;
            for (size_t i = 0; i < Draws; ++i)
            {
                values.push_back(static_cast<uint64_t>(Kans::UUID()));
                valid32[worker] = valid32[worker] && static_cast<uint32_t>(Kans::UUID32()) != 0;
            }
        });
        std::unordered_set<uint64_t> unique;
        bool highBit = false;
        for (size_t worker = 0; worker < WorkerCount; ++worker)
        {
            EXPECT_TRUE(valid32[worker]);
            for (uint64_t value : identities[worker])
            {
                EXPECT_NE(value, 0u);
                EXPECT_TRUE(unique.insert(value).second);
                highBit = highBit || (value >> 63) != 0;
            }
        }
        EXPECT_TRUE(highBit);
        EXPECT_EQ(static_cast<uint64_t>(Kans::UUID(0)), 0u);
        EXPECT_EQ(static_cast<uint32_t>(Kans::UUID32(0)), 0u);
    }

    TEST(CoreIdentityConcurrency, ConcurrentHashFirstUsePreservesPersistedResults)
    {
        const std::array<std::string, 4> inputs {"", "abc", "textures/albedo.png", std::string(200, 'x')};
        struct Result
        {
            std::string Full;
            uint64_t Short = 0;
        };
        std::array<std::array<Result, 4>, WorkerCount> outputs;
        RunTogether([&](size_t worker) {
            for (size_t round = 0; round < 256; ++round)
                for (size_t i = 0; i < inputs.size(); ++i)
                {
                    // Exercise both entry points concurrently during initialization.
                    if (worker % 2 == 0)
                        outputs[worker][i].Full = Kans::Hash::GenerateMD5Hash(inputs[i]);
                    outputs[worker][i].Short = Kans::Hash::Generate64MD5Hash(inputs[i]);
                    if (worker % 2 != 0)
                        outputs[worker][i].Full = Kans::Hash::GenerateMD5Hash(inputs[i]);
                }
        });
        for (const auto& worker : outputs)
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                EXPECT_EQ(worker[i].Full, Kans::Hash::GenerateMD5Hash(inputs[i]));
                EXPECT_EQ(worker[i].Short, Kans::Hash::Generate64MD5Hash(inputs[i]));
            }
        // The existing short hash stores the first MD5 state word, not a new 64-bit digest.
        EXPECT_EQ(Kans::Hash::Generate64MD5Hash(""), 0xd98c1dd4u);
        EXPECT_EQ(Kans::Hash::Generate64MD5Hash("abc"), 0x98500190u);
    }
} // namespace
