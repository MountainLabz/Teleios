#pragma once
#ifndef CHUNK_BUFFER_MANAGER_H
#define CHUNK_BUFFER_MANAGER_H

#include <vector>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <glad/glad.h>
#include <unordered_map>

// Manages a persistently-mapped SSBO ring buffer for quad data (uint64_t per quad).
class ChunkBufferManager {
public:
    struct ChunkMeshInfo {
        uint32_t slotOffset;    // Starting slot in SSBO (one slot = one uint64_t quad)
        uint32_t quadCount;     // Number of quads for this chunk
    };

    // Initialize the SSBO ring buffer to hold maxQuads uint64_t entries
    // Must be called after GL context creation.
    void initialize(size_t maxQuads) {
        totalSlots = maxQuads;
        bufferSize = maxQuads * sizeof(uint64_t);
        glCreateBuffers(1, &ssbo);
        glNamedBufferStorage(ssbo,
            bufferSize,
            nullptr,
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT);
        mappedPtr = reinterpret_cast<uint64_t*>(
            glMapNamedBufferRange(ssbo, 0, bufferSize,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));
        writeCursor = 0;
    }

    // Stage quad data for a chunk: copy quads into the ring buffer at current cursor,
    // record offset and count, then advance cursor (wrapping if necessary).
    void stageChunkData(uint64_t chunkKey,
                        const std::vector<uint64_t>& quads,
                        uint32_t poolSlotOffset) {
        std::lock_guard<std::mutex> lock(mutex);
        size_t byteCount = quads.size() * sizeof(uint64_t);
        // Wrap if not enough space at end
        if ((writeCursor + byteCount) > bufferSize) {
            writeCursor = 0;
        }
        // Copy directly into GPU-mapped memory
        std::memcpy(mappedPtr + (writeCursor / sizeof(uint64_t)),
                    quads.data(), byteCount);
        // Record info
        meshInfos[chunkKey] = { static_cast<uint32_t>(writeCursor / sizeof(uint64_t)),
                                 static_cast<uint32_t>(quads.size()) };
        // Advance cursor
        writeCursor += byteCount;
    }

    // Bind the SSBO to a specified binding point for your shader to read
    void bind(GLuint bindingPoint) const {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, ssbo);
    }

    // Retrieve per-chunk mesh info (slot offsets and counts)
    const std::unordered_map<uint64_t, ChunkMeshInfo>& getMeshInfos() const {
        return meshInfos;
    }

    // Clear staged infos (to start fresh next frame)
    void clear() {
        meshInfos.clear();
    }

    // Destroy the SSBO and unmap
    void destroy() {
        if (mappedPtr) {
            glUnmapNamedBuffer(ssbo);
            mappedPtr = nullptr;
        }
        if (ssbo) {
            glDeleteBuffers(1, &ssbo);
            ssbo = 0;
        }
        meshInfos.clear();
    }

private:
    GLuint ssbo = 0;
    uint64_t* mappedPtr = nullptr;
    size_t bufferSize = 0;
    size_t totalSlots = 0;
    size_t writeCursor = 0;
    std::mutex mutex;
    // Map from application chunk ID or hash key to mesh slot info
    std::unordered_map<uint64_t, ChunkMeshInfo> meshInfos;
};

#endif // CHUNK_BUFFER_MANAGER_H
