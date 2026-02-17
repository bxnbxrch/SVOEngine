#include "vox/SparseVoxelOctree.h"
#include <queue>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>

namespace vox {

static uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t emissive) {
    return (static_cast<uint32_t>(emissive) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

SparseVoxelOctree::SparseVoxelOctree(uint32_t depth) 
    : m_depth(depth) {
    // Allocate root node
    m_nodes.push_back({0});
}

uint32_t SparseVoxelOctree::getOrAddColor(uint32_t color) {
    auto it = m_colorToIndex.find(color);
    if (it != m_colorToIndex.end()) {
        return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(m_colors.size());
    m_colors.push_back(color);
    m_colorToIndex[color] = idx;
    return idx;
}

void SparseVoxelOctree::setVoxel(glm::uvec3 pos, uint32_t color) {
    uint32_t colorIdx = getOrAddColor(color);

    if ((color & 0xFF000000u) != 0u) {
        m_emissiveVoxels.push_back(pos);
    }

    uint32_t nodeIdx = 0;
    // Descend 7 levels (bits 7 down to 1), then mark bit-0 node as leaf
    for (int level = m_depth - 1; level > 0; --level) {  
        OctreeNode& node = m_nodes[nodeIdx];
        
        // Compute which child (0-7)
        uint32_t childIdx = ((pos.x >> level) & 1) * 4 + 
                            ((pos.y >> level) & 1) * 2 + 
                            ((pos.z >> level) & 1);

        // Extract children pointer (mask out flag bits)
        uint32_t childPtr = node.data & 0x3FFFFFFFu;
        
        if (!childPtr) {
            // Create 8 empty children
            childPtr = (uint32_t)m_nodes.size();
            node.data = childPtr;
            for (int i = 0; i < 8; ++i) m_nodes.push_back({0});
        }

        nodeIdx = childPtr + childIdx;
    }

    // Leaf: store color index
    m_nodes[nodeIdx].data = OctreeNode::LEAF_BIT | colorIdx;
}

void SparseVoxelOctree::generateTestScene() {
    // Translate the small test clusters so they are centered inside the SVO
    glm::uvec3 base(120u, 120u, 120u);

    // Create a few test voxel blocks at different positions (all offset by base)
    // Red base block
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF0000);

    // Green block to the side
    for (int x = 8; x < 12; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x00FF00);

    // Blue block higher
    for (int x = 4; x < 8; ++x)
        for (int y = 8; y < 12; ++y)
            for (int z = 4; z < 8; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x0000FF);

    // Yellow block
    for (int x = 0; x < 8; ++x)
        for (int y = 12; y < 16; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFFFF00);

    // Magenta pillar
    for (int x = 16; x < 20; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF00FF);

    // Cyan tower
    for (int x = 20; x < 24; ++x)
        for (int y = 0; y < 20; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x00FFFF);

    // Orange stairs
    for (int i = 0; i < 8; ++i)
        for (int x = 24; x < 28; ++x)
            for (int y = i * 2; y < i * 2 + 2; ++y)
                for (int z = 0; z < 4; ++z)
                    setVoxel({(uint32_t)(x + i) + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF8000);

    // Single emissive voxel placed next to the test scene
    setVoxel({base.x + 1u, base.y + 1u, base.z + 1u}, packColor(255, 255, 255, 255));
    
    // Mark homogeneous nodes for traversal optimization
    markHomogeneousNodes();
}

bool SparseVoxelOctree::loadFromVoxFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open .vox file: " << filepath << std::endl;
        return false;
    }

    // Read header
    char magic[4];
    file.read(magic, 4);
    if (std::memcmp(magic, "VOX ", 4) != 0) {
        std::cerr << "Invalid .vox file magic" << std::endl;
        return false;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), 4);
    std::cout << "Loading MagicaVoxel file version " << version << std::endl;

    // Read MAIN chunk
    char chunkId[4];
    file.read(chunkId, 4);
    if (std::memcmp(chunkId, "MAIN", 4) != 0) {
        std::cerr << "Expected MAIN chunk" << std::endl;
        return false;
    }

    uint32_t mainChunkSize, mainChildrenSize;
    file.read(reinterpret_cast<char*>(&mainChunkSize), 4);
    file.read(reinterpret_cast<char*>(&mainChildrenSize), 4);

    uint32_t sizeX = 0, sizeY = 0, sizeZ = 0;
    bool sizeLoaded = false;
    bool voxelsLoaded = false;

    struct VoxelData { uint8_t x, y, z, colorIndex; };
    std::vector<VoxelData> voxelData;
    uint32_t numVoxels = 0;

    std::vector<uint32_t> palette(256);
    bool paletteLoaded = false;

    uint32_t bytesRead = 0;
    while (bytesRead < mainChildrenSize && file) {
        file.read(chunkId, 4);
        if (!file) break;

        uint32_t chunkSize = 0;
        uint32_t childrenSize = 0;
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        file.read(reinterpret_cast<char*>(&childrenSize), 4);

        bytesRead += 12 + chunkSize + childrenSize;

        if (std::memcmp(chunkId, "SIZE", 4) == 0) {
            file.read(reinterpret_cast<char*>(&sizeX), 4);
            file.read(reinterpret_cast<char*>(&sizeY), 4);
            file.read(reinterpret_cast<char*>(&sizeZ), 4);
            sizeLoaded = true;
            std::cout << "Model size: " << sizeX << " x " << sizeY << " x " << sizeZ << std::endl;

            if (chunkSize > 12) {
                file.seekg(chunkSize - 12, std::ios::cur);
            }
        } else if (std::memcmp(chunkId, "XYZI", 4) == 0) {
            file.read(reinterpret_cast<char*>(&numVoxels), 4);
            std::cout << "Loading " << numVoxels << " voxels..." << std::endl;

            voxelData.resize(numVoxels);
            file.read(reinterpret_cast<char*>(voxelData.data()), numVoxels * 4);
            voxelsLoaded = true;

            uint32_t bytesConsumed = 4 + numVoxels * 4;
            if (chunkSize > bytesConsumed) {
                file.seekg(chunkSize - bytesConsumed, std::ios::cur);
            }
        } else if (std::memcmp(chunkId, "RGBA", 4) == 0) {
            for (int i = 0; i < 256; ++i) {
                uint8_t r, g, b, a;
                file.read(reinterpret_cast<char*>(&r), 1);
                file.read(reinterpret_cast<char*>(&g), 1);
                file.read(reinterpret_cast<char*>(&b), 1);
                file.read(reinterpret_cast<char*>(&a), 1);
                uint8_t emissive = (r == 255 && g == 255 && b == 255) ? 255 : 0;
                palette[i] = packColor(r, g, b, emissive);
            }
            paletteLoaded = true;
            std::cout << "Loaded RGBA palette" << std::endl;

            if (chunkSize > 1024) {
                file.seekg(chunkSize - 1024, std::ios::cur);
            }
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }

        if (childrenSize > 0) {
            file.seekg(childrenSize, std::ios::cur);
        }
    }

    if (!sizeLoaded) {
        std::cerr << "Expected SIZE chunk" << std::endl;
        return false;
    }

    if (!voxelsLoaded) {
        std::cerr << "Expected XYZI chunk" << std::endl;
        return false;
    }
    
    if (!paletteLoaded) {
        // Default palette if RGBA chunk not found
        for (int i = 0; i < 256; ++i) {
            uint8_t r = (i * 127) % 256;
            uint8_t g = (i * 191) % 256;
            uint8_t b = (i * 223) % 256;
            uint8_t emissive = (r == 255 && g == 255 && b == 255) ? 255 : 0;
            palette[i] = packColor(r, g, b, emissive);
        }
        std::cout << "Using default palette" << std::endl;
    }

    // Calculate offset to center the model in the grid
    uint32_t gridSize = 1u << m_depth; // 2048 for depth 11
    
    // First pass: find actual bounding box of voxel data
    uint8_t minX = 255, minY = 255, minZ = 255;
    uint8_t maxX = 0, maxY = 0, maxZ = 0;
    for (uint32_t i = 0; i < numVoxels; ++i) {
        if (voxelData[i].colorIndex == 0) continue;
        minX = std::min(minX, voxelData[i].x);
        maxX = std::max(maxX, voxelData[i].x);
        minY = std::min(minY, voxelData[i].y);
        maxY = std::max(maxY, voxelData[i].y);
        minZ = std::min(minZ, voxelData[i].z);
        maxZ = std::max(maxZ, voxelData[i].z);
    }
    
    uint32_t actualSizeX = maxX - minX + 1;
    uint32_t actualSizeY = maxY - minY + 1;
    uint32_t actualSizeZ = maxZ - minZ + 1;
    
    // After Z->Y conversion, actualSizeZ becomes height (Y axis), actualSizeY becomes depth (Z axis)
    uint32_t instanceWidth = actualSizeX;   // X stays X
    uint32_t instanceHeight = actualSizeZ;  // Z becomes Y
    uint32_t instanceDepth = actualSizeY;   // Y becomes Z
    
    std::cout << "Original bounds: [" << (int)minX << "," << (int)maxX << "] ["
              << (int)minY << "," << (int)maxY << "] [" << (int)minZ << "," << (int)maxZ << "]" << std::endl;
    std::cout << "Actual size: " << actualSizeX << "x" << actualSizeY << "x" << actualSizeZ 
              << " -> " << instanceWidth << "x" << instanceHeight << "x" << instanceDepth << " (after transform)" << std::endl;
    
    // Read voxel data
    int voxelsAdded = 0;
    int pureWhiteCount = 0;
    
    // Process each voxel
    for (uint32_t i = 0; i < numVoxels; ++i) {
        uint8_t x = voxelData[i].x;
        uint8_t y = voxelData[i].y;
        uint8_t z = voxelData[i].z;
        uint8_t colorIndex = voxelData[i].colorIndex;

        // MagicaVoxel uses 1-based color indices (0 = empty), so use colorIndex-1
        if (colorIndex == 0) continue;  // Skip empty voxels

        // Normalize to bounding box relative coordinates
        int32_t relX = x - minX;
        int32_t relY = y - minY;
        int32_t relZ = z - minZ;
        
        // MagicaVoxel uses Z-up, convert to Y-up
        // Flip Z to fix upside-down issue
        int32_t voxX = relX;
        int32_t voxY = (actualSizeZ - 1 - relZ);  // Z becomes Y, flipped
        int32_t voxZ = relY;  // Y becomes Z

        // Bounds check
        if (voxX >= 0 && voxX < (int32_t)gridSize &&
            voxY >= 0 && voxY < (int32_t)gridSize &&
            voxZ >= 0 && voxZ < (int32_t)gridSize) {
            
            uint32_t color = palette[colorIndex - 1];  // Adjust for 1-based indexing
            if ((color & 0x00FFFFFFu) == 0x00FFFFFFu) {
                ++pureWhiteCount;
            }
            setVoxel({(uint32_t)voxX, (uint32_t)voxY, (uint32_t)voxZ}, color);
            voxelsAdded++;
        }
    }
    
    // Add a single emissive voxel adjacent to the model
    uint32_t lightX = static_cast<uint32_t>(std::min<int32_t>(static_cast<int32_t>(instanceWidth), gridSize - 1));
    uint32_t lightY = static_cast<uint32_t>(std::min<int32_t>(static_cast<int32_t>(instanceHeight) / 2, gridSize - 1));
    uint32_t lightZ = static_cast<uint32_t>(std::min<int32_t>(static_cast<int32_t>(instanceDepth) / 2, gridSize - 1));
    setVoxel({lightX, lightY, lightZ}, packColor(255, 255, 255, 255));

    std::cout << "Successfully loaded " << filepath << " (" << voxelsAdded << " voxels added)" << std::endl;
    std::cout << "Pure white voxels: " << pureWhiteCount << std::endl;
    std::cout << "Octree has " << m_nodes.size() << " nodes and " << m_colors.size() << " unique colors (palette)" << std::endl;
    std::cout << "Color deduplication ratio: " << (voxelsAdded / (float)m_colors.size()) << ":1" << std::endl;
    
    // Mark homogeneous nodes for traversal optimization
    markHomogeneousNodes();
    
    return true;
}

void SparseVoxelOctree::markHomogeneousNodes() {
    uint32_t markedCount = 0;
    uint32_t compressedCount = 0;
    
    // Process nodes from deepest to root (bottom-up)
    // For each non-leaf node, check if all 8 children are identical leaves
    for (int32_t i = m_nodes.size() - 1; i >= 0; --i) {
        OctreeNode& node = m_nodes[i];
        
        // Skip if already a leaf
        if (node.data & OctreeNode::LEAF_BIT) continue;
        
        uint32_t childPtr = node.data & 0x3FFFFFFFu;
        if (childPtr == 0 || childPtr + 7 >= m_nodes.size()) continue;
        
        // Check if all 8 children are identical leaves with same color
        bool allSameLeaves = true;
        uint32_t firstChild = m_nodes[childPtr].data;
        
        // First child must be a leaf
        if (!(firstChild & OctreeNode::LEAF_BIT)) continue;
        
        for (uint32_t j = 1; j < 8; ++j) {
            if (m_nodes[childPtr + j].data != firstChild) {
                allSameLeaves = false;
                break;
            }
        }
        
        // If all children are identical leaves, compress this node to a leaf
        if (allSameLeaves) {
            uint32_t colorIdx = firstChild & 0x3FFFFFFFu;
            // Convert this node to a leaf with the same color, mark as homogeneous
            node.data = OctreeNode::LEAF_BIT | OctreeNode::HOMOGENEOUS_BIT | colorIdx;
            compressedCount++;
            markedCount++;
            // Note: We don't delete children here to avoid invalidating indices
            // In a production system, you'd compact the node array
        } else {
            // Just mark as homogeneous but keep children for traversal
            node.data |= OctreeNode::HOMOGENEOUS_BIT;
            markedCount++;
        }
    }
    
    std::cout << "Marked " << markedCount << " homogeneous nodes (" 
              << compressedCount << " compressed to leaves)" << std::endl;
}

} // namespace vox
