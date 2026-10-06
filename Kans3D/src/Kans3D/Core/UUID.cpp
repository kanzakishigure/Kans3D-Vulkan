#include "kspch.h"
#include "UUID.h"

#include <random>
#include <array>
#include <limits>
#include <mutex>
namespace Kans
{

    namespace
    {
        uint64_t GenerateIdentity()
        {
            struct Generator
            {
                std::mutex      Mutex;
                std::mt19937_64 Engine;

                Generator()
                {
                    std::random_device device;
                    std::array<uint32_t, 8> entropy {};
                    for (auto& word : entropy)
                        word = device();
                    std::seed_seq seed(entropy.begin(), entropy.end());
                    Engine.seed(seed);
                }
            };
            // Function-local initialization and every engine draw are synchronized.
            static Generator generator;
            std::lock_guard<std::mutex> lock(generator.Mutex);
            std::uniform_int_distribution<uint64_t> distribution(1, (std::numeric_limits<uint64_t>::max)());
            return distribution(generator.Engine);
        }
    } // namespace

    // 默认构造函数提供一个uuid
    UUID::UUID() : m_UUID(GenerateIdentity()) {}

    UUID::UUID(uint64_t uuid) : m_UUID(uuid) {}

    UUID::UUID(const UUID& other) : m_UUID(other.m_UUID) {}

    UUID32::UUID32()
    {
        do
        {
            m_UUID = static_cast<uint32_t>(GenerateIdentity());
        } while (m_UUID == 0);
    }

    UUID32::UUID32(uint32_t uuid) : m_UUID(uuid) {}

    UUID32::UUID32(const UUID32& other) : m_UUID(other.m_UUID) {}

} // namespace Kans
