#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace vox {

// Simple dense octree node: 8 children or leaf voxel color
struct OctreeNode {
    static constexpr uint32_t LEAF_BIT = 0x80000000u;
    static constexpr uint32_t HOMOGENEOUS_BIT = 0x40000000u;
    uint32_t data; // bit31: isLeaf, bit30: homogeneous, bits[29:0]: child pointer or color index
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
    
    // Get voxel colors (EERGBB, emissive in high byte)
    const std::vector<uint32_t>& getColors() const { return m_colors; }

    // Emissive voxel positions (grid coordinates)
    const std::vector<glm::uvec3>& getEmissiveVoxels() const { return m_emissiveVoxels; }
    
    uint32_t getDepth() const { return m_depth; }
    uint32_t getRootNodeIndex() const { return 0; }
    
    // Mark homogeneous nodes for optimization
    void markHomogeneousNodes();

private:
    uint32_t m_depth;
    std::vector<OctreeNode> m_nodes;
    std::vector<uint32_t> m_colors; // Color palette (unique colors only)
    std::unordered_map<uint32_t, uint32_t> m_colorToIndex; // Color -> palette index
    std::vector<glm::uvec3> m_emissiveVoxels;

    void setVoxel(glm::uvec3 pos, uint32_t color);
    uint32_t getOrAddColor(uint32_t color);
};

} // namespace vox
