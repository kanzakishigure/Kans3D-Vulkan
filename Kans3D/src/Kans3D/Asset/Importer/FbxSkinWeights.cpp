#include "kspch.h"
#include "FbxSkinWeights.h"
#include <stb_image.h>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace Kans
{
    namespace
    {
        constexpr size_t Budget = 512ull * 1024 * 1024;
        struct Property
        {
            char     Type  = 0;
            size_t   Begin = 0, End = 0, Data = 0, Count = 0, Bytes = 0;
            uint32_t Encoding = 0;
        };
        struct Node
        {
            std::string_view      Name;
            size_t                End        = 0;
            bool                  Terminator = false;
            std::vector<Property> Properties;
            std::vector<Node>     Children;
        };
        class BinaryFbx
        {
            const std::vector<char>&                      Source;
            bool                                          Wide   = false;
            size_t                                        Header = 13, Nodes = 0;
            std::unordered_map<size_t, std::vector<char>> Patches;
            static void                                   Require(bool valid)
            {
                if (!valid)
                    throw std::runtime_error("Invalid binary FBX structure or array");
            }
            uint64_t Word(size_t at, size_t bytes) const
            {
                Require(at <= Source.size() && bytes <= Source.size() - at);
                uint64_t result = 0;
                for (size_t i = 0; i < bytes; ++i)
                    result |= uint64_t(uint8_t(Source[at + i])) << (8 * i);
                return result;
            }
            static size_t Stride(char type)
            {
                switch (type)
                {
                    case 'Y':
                        return 2;
                    case 'C':
                    case 'b':
                    case 'c':
                        return 1;
                    case 'I':
                    case 'F':
                    case 'i':
                    case 'f':
                        return 4;
                    case 'L':
                    case 'D':
                    case 'l':
                    case 'd':
                        return 8;
                    default:
                        return 0;
                }
            }
            std::vector<Node> ReadNodes(size_t begin, size_t end, size_t depth)
            {
                Require(depth <= 256 && end <= Source.size());
                std::vector<Node> result;
                for (size_t pos = begin; pos < end;)
                {
                    Require(Header <= end - pos);
                    const size_t width = Wide ? 8 : 4;
                    auto         stop  = Word(pos, width);
                    if (!stop)
                        break;
                    Require(++Nodes <= 1000000 && stop <= end && stop > pos + Header);
                    auto       count = Word(pos + width, width), bytes = Word(pos + width * 2, width);
                    const auto nameBytes = Word(pos + Header - 1, 1);
                    size_t     cursor    = pos + Header;
                    Require(nameBytes <= stop - cursor);
                    Node node;
                    node.End  = size_t(stop);
                    node.Name = {Source.data() + cursor, size_t(nameBytes)};
                    cursor += size_t(nameBytes);
                    Require(bytes <= stop - cursor && count <= 1000000);
                    const size_t propertiesEnd = cursor + size_t(bytes);
                    for (uint64_t i = 0; i < count; ++i)
                    {
                        Require(cursor < propertiesEnd);
                        Property p;
                        p.Begin           = cursor;
                        p.Type            = Source[cursor++];
                        const auto stride = Stride(p.Type);
                        if (p.Type == 'S' || p.Type == 'R')
                        {
                            p.Bytes = size_t(Word(cursor, 4));
                            cursor += 4;
                            p.Data = cursor;
                            Require(cursor <= propertiesEnd && p.Bytes <= propertiesEnd - cursor);
                            cursor += p.Bytes;
                        }
                        else if (p.Type >= 'a' && p.Type <= 'z' && stride)
                        {
                            p.Count    = size_t(Word(cursor, 4));
                            p.Encoding = uint32_t(Word(cursor + 4, 4));
                            p.Bytes    = size_t(Word(cursor + 8, 4));
                            cursor += 12;
                            p.Data = cursor;
                            Require(cursor <= propertiesEnd && p.Bytes <= propertiesEnd - cursor &&
                                    p.Count <= Budget / stride && p.Encoding <= 1);
                            cursor += p.Bytes;
                        }
                        else
                        {
                            Require(stride && stride <= propertiesEnd - cursor);
                            p.Data  = cursor;
                            p.Bytes = stride;
                            cursor += stride;
                        }
                        p.End = cursor;
                        node.Properties.push_back(p);
                    }
                    Require(cursor == propertiesEnd);
                    node.Children = ReadNodes(cursor, size_t(stop), depth + 1);
                    if (stop - cursor >= Header)
                        node.Terminator = std::all_of(Source.begin() + size_t(stop) - Header,
                                                      Source.begin() + size_t(stop),
                                                      [](char c) { return c == 0; });
                    result.push_back(std::move(node));
                    pos = size_t(stop);
                }
                return result;
            }
            std::string_view Text(const Property& p) const
            {
                Require(p.Type == 'S');
                return {Source.data() + p.Data, p.Bytes};
            }
            int64_t Integer(const Property& p) const
            {
                Require(p.Type == 'L' || p.Type == 'I');
                return int64_t(Word(p.Data, p.Bytes));
            }
            std::vector<char> Array(const Property& p) const
            {
                const auto stride = Stride(p.Type);
                Require(stride && p.Count <= Budget / stride);
                std::vector<char> result(p.Count * stride);
                if (p.Encoding == 0)
                {
                    Require(result.size() == p.Bytes);
                    std::memcpy(result.data(), Source.data() + p.Data, p.Bytes);
                }
                else
                {
                    Require(result.size() <= INT_MAX && p.Bytes <= INT_MAX);
                    Require(stbi_zlib_decode_buffer(
                                result.data(), int(result.size()), Source.data() + p.Data, int(p.Bytes)) ==
                            int(result.size()));
                }
                return result;
            }
            const Property* Field(const Node& node, std::string_view name) const
            {
                for (const auto& child : node.Children)
                    if (child.Name == name && child.Properties.size() == 1)
                        return &child.Properties[0];
                return nullptr;
            }
            static void EmitWord(std::vector<char>& out, uint64_t value, size_t bytes)
            {
                for (size_t i = 0; i < bytes; ++i)
                    out.push_back(char((value >> (i * 8)) & 255));
            }
            void PatchArray(const Property& p, const std::vector<size_t>& keep)
            {
                const auto        raw    = Array(p);
                const auto        stride = Stride(p.Type);
                std::vector<char> patch;
                patch.push_back(p.Type);
                EmitWord(patch, keep.size(), 4);
                EmitWord(patch, 0, 4);
                EmitWord(patch, keep.size() * stride, 4);
                for (auto i : keep)
                    patch.insert(patch.end(), raw.begin() + i * stride, raw.begin() + (i + 1) * stride);
                Patches.emplace(p.Begin, std::move(patch));
            }
            void WriteNode(const Node& node, std::vector<char>& out)
            {
                const auto start = out.size();
                out.resize(start + Header, 0);
                out.insert(out.end(), node.Name.begin(), node.Name.end());
                const auto propertiesBegin = out.size();
                size_t     count           = node.Properties.size();
                for (const auto& p : node.Properties)
                    if (auto found = Patches.find(p.Begin); found != Patches.end())
                        out.insert(out.end(), found->second.begin(), found->second.end());
                    else
                        out.insert(out.end(), Source.begin() + p.Begin, Source.begin() + p.End);
                if (node.Name == "Objects" && node.Properties.empty())
                {
                    // Assimp 6's read-all-materials path evaluates the implicit ID-0
                    // container as an object. Supply its harmless root identity so
                    // unused authored materials can be retained as well.
                    out.push_back('L');
                    EmitWord(out, 0, 8);
                    for (int i = 0; i < 2; ++i)
                    {
                        out.push_back('S');
                        EmitWord(out, 4, 4);
                        out.insert(out.end(), {'R', 'o', 'o', 't'});
                    }
                    count = 3;
                }
                const auto propertyBytes = out.size() - propertiesBegin;
                for (const auto& child : node.Children)
                    WriteNode(child, out);
                if (node.Terminator)
                    out.resize(out.size() + Header, 0);
                Require(out.size() <= Budget);
                const auto width = Wide ? 8 : 4;
                auto       write = [&](size_t offset, uint64_t value) {
                    for (size_t i = 0; i < width; ++i)
                        out[start + offset + i] = char((value >> (8 * i)) & 255);
                };
                write(0, out.size());
                write(width, count);
                write(width * 2, propertyBytes);
                out[start + Header - 1] = char(node.Name.size());
            }

        public:
            explicit BinaryFbx(const std::vector<char>& source) : Source(source)
            {
                Wide   = Word(23, 4) >= 7500;
                Header = Wide ? 25 : 13;
            }
            std::vector<char> Prepare()
            {
                auto        roots       = ReadNodes(27, Source.size(), 0);
                const Node* objects     = nullptr;
                const Node* connections = nullptr;
                for (const auto& node : roots)
                {
                    if (node.Name == "Objects")
                        objects = &node;
                    if (node.Name == "Connections")
                        connections = &node;
                }
                Require(objects && connections);
                std::unordered_map<int64_t, int64_t> parents;
                for (const auto& link : connections->Children)
                    if (link.Name == "C" && link.Properties.size() >= 3 && Text(link.Properties[0]) == "OO")
                        parents[Integer(link.Properties[1])] = Integer(link.Properties[2]);
                std::unordered_map<int64_t, std::vector<uint8_t>> used;
                for (const auto& object : objects->Children)
                {
                    if (object.Name != "Geometry" || object.Properties.size() < 3 ||
                        Text(object.Properties[2]) != "Mesh")
                        continue;
                    const auto vertices = Field(object, "Vertices"), indices = Field(object, "PolygonVertexIndex");
                    Require(vertices && indices && vertices->Count % 3 == 0 && indices->Type == 'i');
                    auto& active = used[Integer(object.Properties[0])];
                    active.resize(vertices->Count / 3, 0);
                    auto raw = Array(*indices);
                    for (size_t i = 0; i < indices->Count; ++i)
                    {
                        uint32_t bits = 0;
                        for (int j = 0; j < 4; ++j)
                            bits |= uint32_t(uint8_t(raw[i * 4 + j])) << (j * 8);
                        const int64_t index  = int32_t(bits);
                        const auto    vertex = size_t(index < 0 ? -index - 1 : index);
                        Require(vertex < active.size());
                        active[vertex] = 1;
                    }
                }
                for (const auto& object : objects->Children)
                {
                    if (object.Name != "Deformer" || object.Properties.size() < 3 ||
                        Text(object.Properties[2]) != "Cluster")
                        continue;
                    const auto id   = Integer(object.Properties[0]);
                    auto       skin = parents.find(id);
                    if (skin == parents.end())
                        continue;
                    auto geometry = parents.find(skin->second);
                    if (geometry == parents.end())
                        continue;
                    auto active = used.find(geometry->second);
                    if (active == used.end())
                        continue;
                    const auto indices = Field(object, "Indexes"), weights = Field(object, "Weights");
                    if (!indices || !weights)
                        continue;
                    Require(indices->Type == 'i' && weights->Type == 'd' && indices->Count == weights->Count);
                    auto                raw = Array(*indices);
                    std::vector<size_t> keep;
                    keep.reserve(indices->Count);
                    for (size_t i = 0; i < indices->Count; ++i)
                    {
                        uint32_t vertex = 0;
                        for (int j = 0; j < 4; ++j)
                            vertex |= uint32_t(uint8_t(raw[i * 4 + j])) << (8 * j);
                        Require(vertex < active->second.size());
                        if (active->second[vertex])
                            keep.push_back(i);
                    }
                    // Weights on loose control points have no rendered vertex. Assimp
                    // sizes its mapping from polygon indices and asserts on these tails.
                    if (keep.size() != indices->Count)
                    {
                        PatchArray(*indices, keep);
                        PatchArray(*weights, keep);
                    }
                }
                std::vector<char> result;
                result.reserve(Source.size() + 1024 * 1024);
                result.insert(result.end(), Source.begin(), Source.begin() + 27);
                for (const auto& node : roots)
                    WriteNode(node, result);
                const auto footer = roots.empty() ? size_t(27) : roots.back().End;
                result.insert(result.end(), Source.begin() + footer, Source.end());
                Require(result.size() <= Budget);
                return result;
            }
        };
    } // namespace
    std::vector<char> PrepareBinaryFbx(const std::vector<char>& source)
    {
        if (source.size() < 27 || std::memcmp(source.data(), "Kaydara FBX Binary  \0\x1a\0", 23) != 0)
            return {};
        return BinaryFbx(source).Prepare();
    }
} // namespace Kans
