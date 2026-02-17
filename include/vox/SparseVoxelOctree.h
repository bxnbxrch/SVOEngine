#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>

namespace vox {

// Simple dense octree node: 8 children or leaf voxel color
struct OctreeNode {
    static constexpr uint32_t LEAF_BIT = 0x80000000u;
    uint32_t data; // MSB: isLeaf, bits[30:0]: child pointer or color index
};

// Sparse voxel octree with fixed grid of voxels at leaves
class SparseVoxelOctree {
public:
    SparseVoxelOctree(uint32_t depth = 8); // 2^depth x 2^depth x 2^depth

    // Populate dummy test scene: some colored voxel blocks
    void generateTestScene();

    // Load from MagicaVoxel .vox file
    bool loadFromVoxFile(const std::string& filepath);

    // Get octree nodes (GPU upload)
    const std::vector<OctreeNode>& getNodes() const { return m_nodes; }
    
    // Get voxel colors (RGB8)
    const std::vector<uint32_t>& getColors() const { return m_colors; }
    
    uint32_t getDepth() const { return m_depth; }
    uint32_t getRootNodeIndex() const { return 0; }

private:
    uint32_t m_depth;
    std::vector<OctreeNode> m_nodes;
    std::vector<uint32_t> m_colors; // RGB packed as 0x00RRGGBB

    void setVoxel(glm::uvec3 pos, uint32_t color);
};

} // namespace vox
