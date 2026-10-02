#include <map>
#include "Kans3D/Utilities/MeshUtils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

// Three split vertices per position, with different unit normals.
// Model preparation and validation are excluded from timing.
int main()
{
    using Function = void (*)(std::vector<Kans::Vertex>&, std::vector<glm::vec3>&);
    std::cout << "algorithm,vertices,samples,median_ms,p95_ms,correct\n";
    bool correct = true;
    for (auto fn : {Function(&Kans::Utils::MeshUtils::SmoothNormal),
                    Function(&Kans::Utils::MeshUtils::SmoothNormalHash)})
    {
        const bool hash = fn == &Kans::Utils::MeshUtils::SmoothNormalHash;
        // Bound quadratic work; the hash path also measures high vertex counts.
        for (unsigned count : {3000u, 12000u, 48000u, 192000u, 768000u})
        {
            if (!hash && count > 12000) continue;
            std::vector<Kans::Vertex> vertices(count);
            for (unsigned i = 0; i < count; ++i)
            {
                vertices[i].Position = {float((i / 3) % 512), float((i / 3) / 512), 0};
                vertices[i].Normal = glm::vec3(0);
                vertices[i].Normal[i % 3] = 1;
            }
            std::vector<double> times;
            bool valid = true;
            for (int sample = -1; sample < 5; ++sample)
            {
                std::vector<glm::vec3> output;
                const auto start = std::chrono::steady_clock::now();
                fn(vertices, output);
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                valid = valid && output.size() == count;
                for (const auto n : output)
                {
                    const auto error = glm::length(n - glm::normalize(glm::vec3(1)));
                    valid = valid && std::isfinite(error) && error < 1e-5f;
                }
                if (sample >= 0) times.push_back(ms);
            }
            std::sort(times.begin(), times.end());
            std::cout << (hash ? "SmoothNormalHash" : "SmoothNormal") << ','
                      << count << ",5," << times[2] << ',' << times[4] << ','
                      << (valid ? "true" : "false") << std::endl;
            correct = correct && valid;
        }
    }
    return correct ? 0 : 1;
}
