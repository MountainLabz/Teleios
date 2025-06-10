
#include "ChunkHandler.h"
#include <cstring>    // for std::memcpy
#include <iostream>   // for debug logging (optional)

const int chunkSize = 62;
// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------


// FastNoiseLite ChunkHandler::sharedNoise(12345); // Seed with 12345
FastNoiseLite ChunkHandler::sharedNoise;

// Definition for the static noiseInitialized member
// Initialize it to false, as it's likely not initialized at program start.
bool ChunkHandler::noiseInitialized = false;
const float ChunkHandler::voxel_scale = 0.1f; // Definition and initialization in .cpp


ChunkHandler::ChunkHandler() = default;


ChunkHandler::~ChunkHandler() { destroy(); }
// ----------------------------------------------------------------------------
// init(maxTotalQuads):
//   - Creates a UniversalPool<uint64_t> with capacity = maxTotalQuads.
//   - Allocates one GL SSBO of size (maxTotalQuads * sizeof(uint64_t)).
// ----------------------------------------------------------------------------
bool ChunkHandler::init(uint32_t maxTotalQuads) {
    // maxTotalQuads is typically for vertex data, not metadata.
    // Ensure UniversalPool and ChunkBufferManager are initialized correctly
    // with appropriate sizes for vertex data.
    pool = new UniversalPool<uint64_t, true>(maxTotalQuads, /*ownsMemory=*/true);
    pool->reset();
    bufferMgr.initialize(maxTotalQuads);

    // Create the metadata SSBO
    glCreateBuffers(1, &metadataSSBO);

    // Define a reasonable maximum for the number of chunks whose metadata will be stored.
    // You should adjust MAX_METADATA_CHUNKS based on your application's expected
    // maximum number of active chunks to avoid reallocations or insufficient space.
    const size_t MAX_METADATA_CHUNKS = 10000; // Example: Supports up to 10,000 chunks for metadata

    // Calculate the total buffer size for the metadata SSBO:
    // - sizeof(uint32_t) for the 'chunkCount'
    // - Plus 12 bytes of padding to ensure the 'ChunkData' array starts on a 16-byte boundary
    //   (this is consistent with std430 layout rules and your glBufferSubData offset of 16).
    // - Plus (MAX_METADATA_CHUNKS * sizeof(ChunkData)) for the array of 'ChunkData' structs.
    // Each ChunkData struct is 32 bytes (glm::ivec4 (16 bytes) + 3 ints (12 bytes) + 4 bytes padding = 32 bytes).
    size_t metadataBufferSize = 16 + MAX_METADATA_CHUNKS * sizeof(ChunkData);

    // Allocate storage for the metadata SSBO
    glNamedBufferStorage(metadataSSBO, metadataBufferSize, nullptr, GL_DYNAMIC_STORAGE_BIT);

    return true;
}


void ChunkHandler::destroy() {
    if (metadataSSBO) {
        glDeleteBuffers(1, &metadataSSBO);
        metadataSSBO = 0;
    }
    bufferMgr.destroy();
    delete pool; pool = nullptr;
    chunkMap.clear();
}

bool ChunkHandler::addOrUpdateChunk(const glm::ivec3& coords,
    const std::vector<uint64_t>& quads) {
    auto it = chunkMap.find(coords);

    // Common allocation logic
    int nodeID;
    if (!pool->allocate(nodeID, static_cast<uint32_t>(quads.size()))) {
        return false; // Failed to allocate pool node
    }
    uint32_t offset = static_cast<uint32_t>(pool->getBlock(nodeID).position);

    uint64_t key = (uint64_t(coords.x) & 0xFFFFF) << 40 |
        (uint64_t(coords.y) & 0xFFFFF) << 20 |
        (uint64_t(coords.z) & 0xFFFFF);
    bufferMgr.stageChunkData(key, quads, offset);

    // Check if the chunk already exists
    if (it != chunkMap.end()) {
        // Chunk exists: Deallocate old mesh data from the pool
        pool->deallocate(it->second.poolNodeID);

        // Update the existing ChunkMetadata object with the new mesh data
        // IMPORTANT: The sdfEdits vector of the existing 'it->second' is preserved.
        it->second.poolNodeID = nodeID;
        it->second.quadCount = static_cast<uint32_t>(quads.size());
        it->second.ssboSlotOffset = offset;
        // No need to touch it->second.sdfEdits as it already contains the history.

        return true;
    }
    else {
        // Chunk does not exist: Create a new ChunkMetadata object
        ChunkMetadata md;
        md.chunkCoords = coords;
        md.poolNodeID = nodeID;
        md.quadCount = static_cast<uint32_t>(quads.size());
        md.ssboSlotOffset = offset;
        md.sdfEdits.clear(); // For a brand new chunk, its SDF edits list is initially empty.
        chunkMap[coords] = std::move(md); // Insert the new chunk metadata

        return true;
    }
}


void ChunkHandler::removeChunk(const glm::ivec3& coords) {
    auto it = chunkMap.find(coords);
    if (it != chunkMap.end()) {
        pool->deallocate(it->second.poolNodeID);
        chunkMap.erase(it);
    }
}

void ChunkHandler::clearAll() {
    for (auto& kv : chunkMap) pool->deallocate(kv.second.poolNodeID);
    chunkMap.clear();
    bufferMgr.clear();
}



void ChunkHandler::bindQuadsSSBO(GLuint bindingPoint) const {
    bufferMgr.bind(bindingPoint);
}

void ChunkHandler::bindMetadataSSBO(GLuint bindingPoint) {
    prepareMetadataBuffer();
    uint32_t count = static_cast<uint32_t>(tempChunkData.size());

    // Bind SSBO for update
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, metadataSSBO);
    // Upload chunkCount (4 bytes)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &count);
    // Upload array of ChunkData starting at offset 16 (4 bytes count + 12 bytes padding)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 16,
        count * sizeof(ChunkData), tempChunkData.data());
    // Bind SSBO to shader slot
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, metadataSSBO);
    // Unbind target
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

size_t ChunkHandler::getLoadedChunkCount() const {
    return chunkMap.size();
}

size_t ChunkHandler::retrieveFirstsAndCounts(std::vector<GLint>& firsts,
    std::vector<GLsizei>& counts) const {
    firsts.clear(); counts.clear();
    firsts.reserve(chunkMap.size()); counts.reserve(chunkMap.size());
    for (auto& kv : chunkMap) {
        firsts.push_back(static_cast<GLint>(kv.second.ssboSlotOffset) * 6);
        counts.push_back(static_cast<GLsizei>(kv.second.quadCount) * 6);
    }
    return chunkMap.size();
}

bool ChunkHandler::prepareChunkMesh(const glm::ivec3& coords) {
    auto& md = chunkMap[coords];
    std::vector<uint8_t> voxels(md.chunkCoords.x * md.chunkCoords.y * md.chunkCoords.z);
    generateVoxelsWithSDF(voxels, 64, 64*64, 64*64*64, coords, sharedNoise, md.sdfEdits);
    auto meshData = generateMeshData(voxels);
    return addOrUpdateChunk(coords, *meshData.vertices);
}

void ChunkHandler::prepareMetadataBuffer() {
    tempChunkData.clear();
    tempChunkData.reserve(chunkMap.size());
    for (auto& kv : chunkMap) {
        ChunkData d;
        d.offset = glm::ivec4(kv.first, 0);
        d.first = kv.second.ssboSlotOffset * 6;
        d.count = kv.second.quadCount * 6;
        d.padding1 = 0;
        d.padding2 = 0;
        tempChunkData.push_back(d);
    }
}



// Structure to define an Axis-Aligned Bounding Box (AABB)
struct AABB {
    int minX, maxX;
    int minY, maxY;
    int minZ, maxZ;
};
// Renamed and modified from generateTerrain to include SDF edits
// Renamed and modified from generateTerrain to include SDF edits
void ChunkHandler::generateVoxelsWithSDF(
    std::vector<uint8_t>& voxels,
    int cs_p_val,    // N (e.g. 64, including padding)
    int cs_p2_val,   // N^2
    int cs_p3_val,   // N^3
    glm::ivec3 chunkOffsetInVoxels, // This is the chunk's base world-voxel coordinate
    FastNoiseLite& noise,
    const std::vector<std::unique_ptr<ISDFEdit>>& sdfEdits // Changed to base class pointer
) {
    auto start = std::chrono::high_resolution_clock::now();
    const int pad = 1; // Assuming padding is 1 voxel on each side
    const int N = cs_p_val; // Total size including padding, e.g., 64

    // 1) Ensure the voxel buffer is exactly N^3, zero-filled
    if (voxels.size() != static_cast<size_t>(cs_p3_val)) {
        voxels.assign(cs_p3_val, 0);
    }
    else {
        std::fill(voxels.begin(), voxels.end(), 0);
    }

    int terrainMaterialType = 2; // Default material for terrain, as per original code

    // 3) Precompute our “height mapping” constants
    const float maxHeightGlobal = static_cast<float>(N) / 2.0f;
    const float baseHeightGlobal = static_cast<float>(N) / 4.0f;

    // 4) Generate base terrain and apply SDF edits in a single pass
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float worldX_noise = (chunkOffsetInVoxels.x + (x - pad)) * ChunkHandler::voxel_scale;
            float worldZ_noise = (chunkOffsetInVoxels.z + (z - pad)) * ChunkHandler::voxel_scale;
            float noiseValue = noise.GetNoise(worldX_noise, worldZ_noise);
            float terrainHeightF = baseHeightGlobal + (noiseValue * maxHeightGlobal * 2.0f);
            int terrainHeightVoxel = static_cast<int>(std::floor(terrainHeightF));

            for (int y = 0; y < N; ++y) {
                size_t index = static_cast<size_t>(z + (x * cs_p_val) + (y * cs_p2_val));
                uint8_t voxelMaterial = 0; // Default to air

                // Determine base terrain material based *only* on height
                int worldY_voxel = chunkOffsetInVoxels.y + (y - pad);
                if (worldY_voxel <= terrainHeightVoxel) {
                    voxelMaterial = static_cast<uint8_t>(terrainMaterialType);
                }

                // Apply SDF edits
                // For each voxel, iterate through all SDF edits and apply them
                glm::vec3 worldVoxelPos_scaled = glm::vec3(
                    (chunkOffsetInVoxels.x + (x - pad)),
                    (chunkOffsetInVoxels.y + (y - pad)),
                    (chunkOffsetInVoxels.z + (z - pad))
                ) * ChunkHandler::voxel_scale;

                for (const auto& edit_ptr : sdfEdits) {
                    float distance = edit_ptr->getSignedDistance(worldVoxelPos_scaled);
                    if (distance <= 0.0f) { // Inside or on the surface of the SDF shape
                        voxelMaterial = edit_ptr->getMaterial();
                    }
                }
                voxels[index] = voxelMaterial;
            }
        }
    }
}

// NEW: Generic addSDFEditToChunk
bool ChunkHandler::addSDFEditToChunk(glm::ivec3 chunkCoords, std::unique_ptr<ISDFEdit> edit,
    int chunkSizeInVoxels, FastNoiseLite& noise) {

    auto it = chunkMap.find(chunkCoords);
    if (it == chunkMap.end()) {
        std::cerr << "[ChunkHandler] Warning: Attempted to add SDF edit to non-existent chunk: ("
            << chunkCoords.x << "," << chunkCoords.y << "," << chunkCoords.z << "). "
            << "Consider creating the chunk first.\n";
        return false;
    }

    // Add the SDF edit to the chunk's list of edits
    it->second.sdfEdits.push_back(std::move(edit)); // Move the unique_ptr

    // Re-calculate the world-voxel offset for this specific chunk
    glm::ivec3 chunkOffsetInVoxels = chunkCoords * chunkSizeInVoxels;

    // Regenerate the mesh for this chunk using its updated SDF edits
    MeshData newMeshData = generateVoxelMesh(1, chunkOffsetInVoxels, noise, it->second.sdfEdits);

    // Update the chunk in the handler (this will deallocate old memory and upload new)
    bool success = addOrUpdateChunk(chunkCoords, *newMeshData.vertices);

    // Clean up dynamically allocated MeshData members
    if (newMeshData.vertices) {
        delete newMeshData.vertices;
        newMeshData.vertices = nullptr;
    }
    if (newMeshData.faceMasks) {
        delete[] newMeshData.faceMasks;
        newMeshData.faceMasks = nullptr;
    }
    if (newMeshData.opaqueMask) {
        delete[] newMeshData.opaqueMask;
        newMeshData.opaqueMask = nullptr;
    }
    if (newMeshData.forwardMerged) {
        delete[] newMeshData.forwardMerged;
        newMeshData.forwardMerged = nullptr;
    }
    if (newMeshData.rightMerged) {
        delete[] newMeshData.rightMerged;
        newMeshData.rightMerged = nullptr;
    }

    if (!success) {
        std::cerr << "[ChunkHandler] ERROR: Failed to update chunk after SDF edit: ("
            << chunkCoords.x << "," << chunkCoords.y << "," << chunkCoords.z << ")\n";
    }
    return success;
}


MeshData ChunkHandler::generateVoxelMesh(
    int option,
    glm::ivec3 chunkOffsetInVoxels,
    FastNoiseLite& noise,
    const std::vector<std::unique_ptr<ISDFEdit>>& sdfEdits // Changed to base class pointer
) {
    const int CS = 62; // Inner chunk size (excluding padding)
    const int CS_P = CS + 2; // Chunk size with padding (64)
    const int CS_P2 = CS_P * CS_P; // 64 * 64
    const int CS_P3 = CS_P * CS_P * CS_P; // 64 * 64 * 64

    std::vector<uint8_t> voxels(CS_P3); // Allocate once

    switch (option) {
    case 1: // Just terrain
        // generateTerrain(voxels, CS_P, CS_P2, CS_P3, chunkOffsetInVoxels, noise);
        // Calling generateVoxelsWithSDF with an empty sdfEdits vector effectively does this
        generateVoxelsWithSDF(voxels, CS_P, CS_P2, CS_P3, chunkOffsetInVoxels, noise, sdfEdits);
        break;
    case 2: // Terrain + SDF edits
        generateVoxelsWithSDF(voxels, CS_P, CS_P2, CS_P3, chunkOffsetInVoxels, noise, sdfEdits);
        break;
    default:
        std::cerr << "Invalid option for generateVoxelMesh" << std::endl;
        // Fallback or error handling
        break;
    }

    return generateMeshData(voxels);
}


void ChunkHandler::profileOpaqueMaskGeneration(
    const std::vector<uint8_t>& voxels,
    int CS_P,
    int CS_P2,
    MeshData& meshData
) {
    // Start timing
    auto start = std::chrono::high_resolution_clock::now();

    for (int z = 0; z < CS_P; ++z) {
        for (int y = 0; y < CS_P; ++y) {
            uint64_t bits = 0;
            for (int x = 0; x < CS_P; ++x) {
                // Accessing voxels[x + (y * CS_P) + (z * CS_P2)] implies a Z-major, then Y, then X ordering.
                // Ensure this matches how your voxels are actually stored if performance is critical.
                // For typical cache-friendly access, X should be the innermost loop.
                if (voxels[x + (y * CS_P) + (z * CS_P2)]) {
                    bits |= (1ull << x);
                }
            }
            meshData.opaqueMask[y + z * CS_P] = bits;
        }
    }

    // Stop timing
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate duration
    std::chrono::duration<double, std::milli> duration = end - start; // Duration in milliseconds

    // Print the duration
    std::cout << "Time taken to generate opaqueMask: " << duration.count() << " ms" << std::endl;
}


// The function
MeshData ChunkHandler::generateMeshData(const std::vector<uint8_t>& voxels) {
    MeshData meshData;

    // Allocate memory
    meshData.faceMasks = new uint64_t[CS_2 * 6];
    meshData.opaqueMask = new uint64_t[CS_P2];
    meshData.forwardMerged = new uint8_t[CS_2];
    meshData.rightMerged = new uint8_t[CS];

    // Calculate maxVertices
    meshData.maxVertices = static_cast<size_t>(CS_P) * CS_P * CS_P * 6 / 2;
    if (meshData.maxVertices < 1024) {
        meshData.maxVertices = 1024;
    }
    meshData.vertices = new BM_VECTOR<uint64_t>(meshData.maxVertices);

    // Initialize allocated memory to zero
    memset(meshData.faceMasks, 0, sizeof(uint64_t) * CS_2 * 6);
    memset(meshData.opaqueMask, 0, sizeof(uint64_t) * CS_P2);
    memset(meshData.forwardMerged, 0, sizeof(uint8_t) * CS_2);
    memset(meshData.rightMerged, 0, sizeof(uint8_t) * CS);

    // Profile opaque mask generation
    for (int z = 0; z < CS_P; ++z) {
        for (int y = 0; y < CS_P; ++y) {
            uint64_t bits = 0;
            for (int x = 0; x < CS_P; ++x) {
                // Accessing voxels[x + (y * CS_P) + (z * CS_P2)] implies a Z-major, then Y, then X ordering.
                // Ensure this matches how your voxels are actually stored if performance is critical.
                // For typical cache-friendly access, X should be the innermost loop.
                if (voxels[x + (y * CS_P) + (z * CS_P2)]) {
                    bits |= (1ull << x);
                }
            }
            meshData.opaqueMask[y + z * CS_P] = bits;
        }
    }


    // Call the mesh function
    mesh(voxels.data(), meshData);

    // Trim the vertices vector
    if (meshData.vertices) {
        meshData.vertices->resize(meshData.vertexCount);
        meshData.vertices->shrink_to_fit();
    }

    return meshData;
}


// ----------------------------------------------------------------------------
// addSDFEditAtWorldPos: Generic function to apply an SDF edit in world space.
// It identifies all affected chunks and their neighbors, then applies the edit.
// ----------------------------------------------------------------------------
bool ChunkHandler::addSDFEditAtWorldPos(const ISDFEdit& edit, int chunkSizeInVoxels, FastNoiseLite& noise) {
    bool allSuccess;
    // Get the tight world-space bounding box from the generic SDF edit object.
    std::pair<glm::vec3, glm::vec3> bounds = edit.getApproximateWorldBounds();
    glm::vec3 worldMin = bounds.first;
    glm::vec3 worldMax = bounds.second;

    // Expand the world bounding box to account for 1-voxel padding required for meshing.
    // Each side of the chunk (min and max) requires a 1-voxel border to correctly mesh.
    // So, we expand by 2 voxels (one on each side) in world units.
    float paddingWorldUnits = 2.0f * ChunkHandler::voxel_scale;
    worldMin -= glm::vec3(paddingWorldUnits);
    worldMax += glm::vec3(paddingWorldUnits);

    // Calculate the world size of a single inner chunk (e.g., 62 * 0.1f)
    float chunkWorldSize = static_cast<float>(62) * ChunkHandler::voxel_scale;

    // Convert the *expanded* world-space bounding box to chunk coordinates (inclusive range)
    glm::ivec3 minChunkCoords = glm::ivec3(
        static_cast<int>(std::floor(worldMin.x / chunkWorldSize)),
        static_cast<int>(std::floor(worldMin.y / chunkWorldSize)),
        static_cast<int>(std::floor(worldMin.z / chunkWorldSize))
    );

    glm::ivec3 maxChunkCoords = glm::ivec3(
        static_cast<int>(std::floor(worldMax.x / chunkWorldSize)),
        static_cast<int>(std::floor(worldMax.y / chunkWorldSize)),
        static_cast<int>(std::floor(worldMax.z / chunkWorldSize))
    );

    // Populate uniqueChunksToProcess directly from this single expanded bounding box
    std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq> uniqueChunksToProcess;
    for (int cx = minChunkCoords.x; cx <= maxChunkCoords.x; ++cx) {
        for (int cy = minChunkCoords.y; cy <= maxChunkCoords.y; ++cy) {
            for (int cz = minChunkCoords.z; cz <= maxChunkCoords.z; ++cz) {
                uniqueChunksToProcess.insert(glm::ivec3(cx, cy, cz));
            }
        }
    }

    std::cout << "--- Processing " << uniqueChunksToProcess.size() << " chunks for SDF edit ---\n";
    for (const auto& currentChunkCoords : uniqueChunksToProcess) {
        // Clone the ISDFEdit object for each chunk. This is crucial because
        // `addSDFEditToChunk` takes ownership of the `unique_ptr`.
        // Each chunk's `sdfEdits` vector needs its own copy of the edit.
        std::unique_ptr<ISDFEdit> clonedEdit = edit.clone();
        if (!clonedEdit) {
            std::cerr << "Error: Failed to clone SDF edit for chunk: ("
                << currentChunkCoords.x << ", " << currentChunkCoords.y << ", " << currentChunkCoords.z << ")\n";
            allSuccess = false;
            continue; // Skip to next chunk if cloning failed
        }

        // Call the generic function to apply the SDF edit to this specific chunk.
        if (!addSDFEditToChunk(currentChunkCoords, std::move(clonedEdit), chunkSizeInVoxels, noise)) {
            // If addSDFEditToChunk fails for any reason (e.g., chunk not found, allocation error),
            // we note it but continue trying for other chunks.
            std::cerr << "Error: Failed to apply SDF edit to chunk: ("
                << currentChunkCoords.x << "," << currentChunkCoords.y << "," << currentChunkCoords.z << ")\n";
            allSuccess = false;
        }
    }

    std::cout << "Total chunks marked for regeneration (or processed): " << uniqueChunksToProcess.size() << "\n";

    return allSuccess;
}
