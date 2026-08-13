#include "ayapch.h"
#include "GDRContext.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Material.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    void GDRContext::Init(VkDevice device, uint32_t framesInFlight, VkBuffer geoPoolBuffer) {
        m_Device = device;

        // ── Create the three shared SSBOs ──
        InstanceSSBO      = std::make_unique<VulkanStorageBuffer>(
            kMaxInstances * sizeof(GPUInstance));
        GeometryRangeSSBO = std::make_unique<VulkanStorageBuffer>(
            kMaxMeshes * sizeof(GeometryRange));
        MaterialSSBO      = std::make_unique<VulkanStorageBuffer>(
            kMaxMaterials * sizeof(GPUMaterial));

        // ── Set=2 descriptor layout: 4 STORAGE_BUFFER bindings ──
        VkDescriptorSetLayoutBinding gdrBindings[4] = {};
        gdrBindings[0].binding = 0; gdrBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        gdrBindings[0].descriptorCount = 1;
        gdrBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        gdrBindings[1].binding = 1; gdrBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        gdrBindings[1].descriptorCount = 1;
        gdrBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        gdrBindings[2].binding = 2; gdrBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        gdrBindings[2].descriptorCount = 1;
        gdrBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        gdrBindings[3].binding = 3; gdrBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        gdrBindings[3].descriptorCount = 1;
        gdrBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutCI.bindingCount = 4;
        layoutCI.pBindings = gdrBindings;
        vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &Set2Layout);

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 4 };
        VkDescriptorPoolCreateInfo poolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolCI.maxSets = framesInFlight;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolCI, nullptr, &Set2Pool);

        Set2Descriptors.resize(framesInFlight);
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, Set2Layout);
        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = Set2Pool;
        alloc.descriptorSetCount = framesInFlight;
        alloc.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &alloc, Set2Descriptors.data());

        // ── Pre-bind VkBuffer handles (unchanging across frames) ──
        for (uint32_t i = 0; i < framesInFlight; i++) {
            VkDescriptorBufferInfo instI{}, rangeI{}, matI{}, geoI{};
            instI.buffer  = InstanceSSBO->GetBuffer(i);
            instI.offset  = 0; instI.range  = VK_WHOLE_SIZE;
            rangeI.buffer = GeometryRangeSSBO->GetBuffer(i);
            rangeI.offset = 0; rangeI.range = VK_WHOLE_SIZE;
            matI.buffer   = MaterialSSBO->GetBuffer(i);
            matI.offset   = 0; matI.range   = VK_WHOLE_SIZE;
            geoI.buffer   = geoPoolBuffer;
            geoI.offset   = 0; geoI.range   = VK_WHOLE_SIZE;

            VkWriteDescriptorSet w[4]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = Set2Descriptors[i]; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[0].descriptorCount = 1; w[0].pBufferInfo = &instI;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = Set2Descriptors[i]; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[1].descriptorCount = 1; w[1].pBufferInfo = &rangeI;
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = Set2Descriptors[i]; w[2].dstBinding = 2;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[2].descriptorCount = 1; w[2].pBufferInfo = &matI;
            w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[3].dstSet = Set2Descriptors[i]; w[3].dstBinding = 3;
            w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[3].descriptorCount = 1; w[3].pBufferInfo = &geoI;
            vkUpdateDescriptorSets(device, 4, w, 0, nullptr);
        }
    }

    void GDRContext::Shutdown() {
        if (Set2Layout && m_Device) { vkDestroyDescriptorSetLayout(m_Device, Set2Layout, nullptr); Set2Layout = VK_NULL_HANDLE; }
        if (Set2Pool && m_Device)   { vkDestroyDescriptorPool(m_Device, Set2Pool, nullptr);  Set2Pool = VK_NULL_HANDLE; }
        InstanceSSBO.reset();
        GeometryRangeSSBO.reset();
        MaterialSSBO.reset();
        Set2Descriptors.clear();
        InstanceCount = 0;
    }

    void GDRContext::BuildFromRenderQueue(const RenderQueue& queue,
                                          GlobalGeometryPool& geoPool,
                                          uint64_t frameNumber) {
        // Guard: skip redundant same-frame rebuilds (monotonic, never wraps).
        if (m_LastBuiltFrameNumber == frameNumber) return;
        m_LastBuiltFrameNumber = frameNumber;

        auto t0 = std::chrono::high_resolution_clock::now();

        // ── Resource staging: ensure all meshes are in the geometry pool ──
        for (const auto& pkt : queue.Packets) {
            if (pkt.MeshAsset) geoPool.GetOrUploadMesh(pkt.MeshAsset.get());
        }

        std::vector<GPUInstance> gdrInstances;
        std::vector<GPUMaterial> gdrMaterials;
        std::vector<GeometryRange> gdrRanges;
        gdrInstances.reserve(queue.Packets.size());
        gdrMaterials.reserve(64);
        gdrRanges.reserve(64);

        std::unordered_map<Mesh*, uint32_t> meshToRange;
        m_MaterialToIndex.clear();

        for (const auto& pkt : queue.Packets) {
            SortKey k; k.Value = pkt.SortKey;
            // All buckets accepted — GPU masks handle per-pass filtering
            if (!pkt.MeshAsset || !pkt.MaterialAsset) continue;

            // Deduplicate meshes → GeometryRange index
            uint32_t rangeIdx;
            auto rit = meshToRange.find(pkt.MeshAsset.get());
            if (rit != meshToRange.end()) {
                rangeIdx = rit->second;
            } else {
                rangeIdx = (uint32_t)gdrRanges.size();
                auto range = geoPool.GetOrUploadMesh(pkt.MeshAsset.get());
                gdrRanges.push_back(range);
                meshToRange[pkt.MeshAsset.get()] = rangeIdx;
            }

            // Deduplicate materials → GPUMaterial index (pointer-based, zero collision)
            uint32_t matIdx;
            auto mit = m_MaterialToIndex.find(pkt.MaterialAsset.get());
            if (mit != m_MaterialToIndex.end()) {
                matIdx = mit->second;
            } else {
                matIdx = (uint32_t)gdrMaterials.size();
                m_MaterialToIndex[pkt.MaterialAsset.get()] = matIdx;
                GPUMaterial gm{};
                auto& b = pkt.MaterialAsset->GetBakedPC();
                gm.albedo    = b.Albedo;
                gm.metallic  = b.Metallic;
                gm.roughness = b.Roughness;
                gm.ao        = b.AO;
                gm.alpha     = b.Alpha;
                gm.albedoBindless    = (int)b.AlbedoMapIndex;
                gm.normalBindless    = (int)b.NormalMapIndex;
                gm.ormBindless       = (int)b.ORMMapIndex;
                gm.metallicBindless  = (int)b.MetallicMapIndex;
                gm.roughnessBindless = (int)b.RoughnessMapIndex;
                gm.aoBindless        = (int)b.AOMapIndex;
                gm.useORMMap    = (int)b.UseORMMap;
                gm.packing       = (uint32_t)b.Packing;
                gm.alphaBindless = (int)b.AlphaMapIndex;
                gm.useAlphaMap   = (b.AlphaMapIndex != 1) ? 1 : 0;
                gm.alphaCutoff  = pkt.MaterialAsset->GetAlphaCutoff();
                gm.blendMode      = (int)pkt.MaterialAsset->GetBlendMode();
                gm.lightModeMask  = pkt.MaterialAsset->GetLightModeMask();
                gdrMaterials.push_back(gm);
            }

            // Build GPUInstance
            GPUInstance gi{};
            gi.transform = pkt.Transform;
            AABB aabb = pkt.MeshAsset->GetAABB();
            glm::vec3 center = (aabb.Min + aabb.Max) * 0.5f;
            glm::vec3 worldCenter = glm::vec3(pkt.Transform * glm::vec4(center, 1.0f));
            // Compute world-space radius: extract max axis scale from the transform matrix
            glm::vec3 scale(
                glm::length(glm::vec3(pkt.Transform[0])),
                glm::length(glm::vec3(pkt.Transform[1])),
                glm::length(glm::vec3(pkt.Transform[2]))
            );
            float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
            float radius = glm::length(aabb.Max - aabb.Min) * 0.5f * maxScale;
            gi.boundingSphere   = glm::vec4(worldCenter, radius);
            gi.geometryRangeIdx = rangeIdx;
            gi.materialIdx      = matIdx;
            gi.entityId         = (uint32_t)(k.Bits.EntityID);
            gi.flags            = 0;
            if (pkt.CastShadows)
                gi.flags |= GPUInstance::kFlag_CastShadows;
            gdrInstances.push_back(gi);
        }

        // Upload to SSBOs (memcpy to persistent-mapped buffers)
        uint32_t instCount     = std::min((uint32_t)gdrInstances.size(), kMaxInstances);
        uint32_t rangeCount    = std::min((uint32_t)gdrRanges.size(),    kMaxMeshes);
        uint32_t materialCount = std::min((uint32_t)gdrMaterials.size(), kMaxMaterials);

        InstanceSSBO->SetData(gdrInstances.data(), instCount * sizeof(GPUInstance));
        GeometryRangeSSBO->SetData(gdrRanges.data(), rangeCount * sizeof(GeometryRange));
        MaterialSSBO->SetData(gdrMaterials.data(), materialCount * sizeof(GPUMaterial));
        InstanceCount = instCount;

        RangeCount = rangeCount;
        MaterialCount = materialCount;
        TotalTriangles = 0; TotalVertices = 0;
        uint32_t emptyRanges = 0;
        for (auto& r : gdrRanges) {
            TotalTriangles += r.indexCount / 3;
            TotalVertices  += r.vertexCount;
            if (r.vertexCount == 0 || r.indexCount == 0) emptyRanges++;
        }
        if (emptyRanges > 0)
            AYAYA_CORE_WARN("GDR: {} of {} ranges EMPTY (vCount==0 or iCount==0)",
                emptyRanges, rangeCount);

        auto t1 = std::chrono::high_resolution_clock::now();
        BuildTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

        VkDeviceSize poolUsed = geoPool.GetUsedBytes();
        VkDeviceSize poolSize = geoPool.GetSize();
        if (poolUsed > poolSize * 3 / 4) {
            AYAYA_CORE_WARN("GDR: GeometryPool {:.1f}MB / {:.1f}MB ({:.0f}%) — approaching capacity!",
                poolUsed / (1024.0*1024.0), poolSize / (1024.0*1024.0),
                100.0 * poolUsed / poolSize);
        }
    }

    void GDRContext::BindSet2(VkCommandBuffer cmd, VkPipelineLayout layout,
                              VkPipelineBindPoint bindPoint, uint32_t frameIndex) const {
        if (frameIndex < Set2Descriptors.size()) {
            vkCmdBindDescriptorSets(cmd, bindPoint, layout,
                2, 1, &Set2Descriptors[frameIndex], 0, nullptr);
        }
    }

    void GDRContext::BuildFromScene(Scene* scene,
                                    GlobalGeometryPool& geoPool,
                                    uint64_t frameNumber) {
        if (!scene) return;
        // Guard: skip redundant same-frame rebuilds (monotonic, never wraps).
        if (m_LastBuiltFrameNumber == frameNumber) return;
        m_LastBuiltFrameNumber = frameNumber;

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<GPUInstance> gdrInstances;
        std::vector<GPUMaterial> gdrMaterials;
        std::vector<GeometryRange> gdrRanges;
        gdrInstances.reserve(4096);
        gdrMaterials.reserve(64);
        gdrRanges.reserve(64);

        std::unordered_map<Mesh*, uint32_t> meshToRange;
        m_MaterialToIndex.clear();
        // Double-buffer: read prev transforms from m_PrevWorldTransforms, write current to new map.
        std::unordered_map<uint32_t, glm::mat4> newPrevTransforms;
        newPrevTransforms.reserve(256);

        auto view = scene->Reg().view<TransformComponent, MeshRendererComponent>();
        uint32_t entitiesFound = 0, entitiesSkipped = 0;
        for (auto entityID : view) {
            Entity entity{ entityID, scene };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.CachedModel)
                meshComp.CachedModel = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            auto model = meshComp.CachedModel;
            if (!model) {
                entitiesSkipped++;
                if (entitiesSkipped <= 3)
                    AYAYA_CORE_WARN("GDR: entity '{}' ModelHandle={} — not in cache",
                        entity.GetComponent<TagComponent>().Tag, (uint64_t)meshComp.ModelHandle);
                continue;
            }
            entitiesFound++;
            if (!meshComp.CachedMaterial)
                meshComp.CachedMaterial = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            auto material = meshComp.CachedMaterial;

            // All blend modes (Opaque, Masked, Translucent) are uploaded to SSBO.
            // GPU compute shaders filter by required LightMode mask — zero CPU cost.
            glm::mat4 transform = entity.GetWorldTransform();
            // Per-object previous-frame transform (motion vector): lookup full entity ID.
            uint32_t fullEntityId = static_cast<uint32_t>(entityID);
            glm::mat4 prevTransform = transform;  // first appearance → no motion
            auto prevIt = m_PrevWorldTransforms.find(fullEntityId);
            if (prevIt != m_PrevWorldTransforms.end())
                prevTransform = prevIt->second;
            newPrevTransforms[fullEntityId] = transform;
            Material* matPtr = material.get();

            for (auto& mesh : model->GetMeshes()) {
                if (gdrInstances.size() >= kMaxInstances) {
                    AYAYA_CORE_WARN("GDR Instance buffer full! {} instances, max {}.",
                        (int)gdrInstances.size(), kMaxInstances);
                    break;
                }

                geoPool.GetOrUploadMesh(mesh.get());

                uint32_t rangeIdx;
                auto rit = meshToRange.find(mesh.get());
                if (rit != meshToRange.end()) {
                    rangeIdx = rit->second;
                } else {
                    rangeIdx = (uint32_t)gdrRanges.size();
                    auto range = geoPool.GetOrUploadMesh(mesh.get());
                    gdrRanges.push_back(range);
                    meshToRange[mesh.get()] = rangeIdx;
                }

                uint32_t matIdx;
                auto mit = m_MaterialToIndex.find(matPtr);
                if (mit != m_MaterialToIndex.end()) {
                    matIdx = mit->second;
                } else {
                    matIdx = (uint32_t)gdrMaterials.size();
                    m_MaterialToIndex[matPtr] = matIdx;
                    GPUMaterial gm{};
                    if (material) {
                        auto& b = material->GetBakedPC();
                        gm.albedo = b.Albedo; gm.metallic = b.Metallic;
                        gm.roughness = b.Roughness; gm.ao = b.AO; gm.alpha = b.Alpha;
                        gm.albedoBindless    = (int)b.AlbedoMapIndex;
                        gm.normalBindless    = (int)b.NormalMapIndex;
                        gm.ormBindless       = (int)b.ORMMapIndex;
                        gm.metallicBindless  = (int)b.MetallicMapIndex;
                        gm.roughnessBindless = (int)b.RoughnessMapIndex;
                        gm.aoBindless        = (int)b.AOMapIndex;
                        gm.useORMMap   = (int)b.UseORMMap;
                        gm.alphaBindless = (int)b.AlphaMapIndex;
                        gm.useAlphaMap   = (b.AlphaMapIndex != 1) ? 1 : 0;
                        gm.alphaCutoff = material->GetAlphaCutoff();
                        gm.blendMode     = (int)material->GetBlendMode();
                        gm.lightModeMask = material->GetLightModeMask();
                        gm.packing       = (uint32_t)b.Packing;
                    }
                    gdrMaterials.push_back(gm);
                }

                GPUInstance gi{};
                gi.transform = transform;
                gi.prevTransform = prevTransform;
                AABB aabb = mesh->GetAABB();
                glm::vec3 center = (aabb.Min + aabb.Max) * 0.5f;
                glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(center, 1.0f));
                glm::vec3 scale(
                    glm::length(glm::vec3(transform[0])),
                    glm::length(glm::vec3(transform[1])),
                    glm::length(glm::vec3(transform[2])));
                float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
                float radius = glm::length(aabb.Max - aabb.Min) * 0.5f * maxScale;
                gi.boundingSphere   = glm::vec4(worldCenter, radius);
                gi.geometryRangeIdx = rangeIdx;
                gi.materialIdx      = matIdx;
                gi.entityId         = static_cast<uint32_t>(entityID) & 0xFFFF;
                gi.flags = 0;
                if (meshComp.CastShadows) gi.flags |= GPUInstance::kFlag_CastShadows;
                if (meshComp.ReceiveShadows) gi.flags |= GPUInstance::kFlag_ReceiveShadows;
                gdrInstances.push_back(gi);
            }
        }

        // Swap prev-transform maps — dropped entities are automatically evicted.
        m_PrevWorldTransforms = std::move(newPrevTransforms);

        uint32_t instCount     = std::min((uint32_t)gdrInstances.size(), kMaxInstances);
        uint32_t rangeCount    = std::min((uint32_t)gdrRanges.size(),    kMaxMeshes);
        uint32_t materialCount = std::min((uint32_t)gdrMaterials.size(), kMaxMaterials);

        InstanceSSBO->SetData(gdrInstances.data(), instCount * sizeof(GPUInstance));
        GeometryRangeSSBO->SetData(gdrRanges.data(), rangeCount * sizeof(GeometryRange));
        MaterialSSBO->SetData(gdrMaterials.data(), materialCount * sizeof(GPUMaterial));
        InstanceCount = instCount;

        RangeCount = rangeCount;
        MaterialCount = materialCount;
        TotalTriangles = 0; TotalVertices = 0;
        uint32_t emptyRanges = 0;
        for (auto& r : gdrRanges) {
            TotalTriangles += r.indexCount / 3;
            TotalVertices  += r.vertexCount;
            if (r.vertexCount == 0 || r.indexCount == 0) emptyRanges++;
        }
        if (emptyRanges > 0)
            AYAYA_CORE_WARN("GDR: {} of {} ranges EMPTY (vCount==0 or iCount==0)",
                emptyRanges, rangeCount);

        auto t1 = std::chrono::high_resolution_clock::now();
        BuildTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

        if (entitiesSkipped > 0) {
            AYAYA_CORE_WARN("GDR BuildFromScene: {} entities found, {} SKIPPED (ModelHandle not in cache)",
                entitiesFound, entitiesSkipped);
        }

        VkDeviceSize poolUsed = geoPool.GetUsedBytes();
        VkDeviceSize poolSize = geoPool.GetSize();
        if (poolUsed > poolSize * 3 / 4) {
            AYAYA_CORE_WARN("GDR: GeometryPool {:.1f}MB / {:.1f}MB ({:.0f}%) — approaching capacity!",
                poolUsed / (1024.0*1024.0), poolSize / (1024.0*1024.0),
                100.0 * poolUsed / poolSize);
        }
    }

    uint32_t GDRContext::GetMaterialSSBOIndex(const Material* mat) const {
        auto it = m_MaterialToIndex.find(mat);
        return (it != m_MaterialToIndex.end()) ? it->second : 0;
    }

    void GDRContext::ResetCachesAndForceRebuild() {
        // Clear per-scene material→SSBO mapping.
        // Material* pointers are stable (owned by AssetManager) but the SSBO
        // index assignment is scene-specific — materials from the old scene
        // must not leak into the new scene's MaterialSSBO layout.
        m_MaterialToIndex.clear();

        // Reset counters so the next BuildFromScene produces a clean SSBO
        // state for the new (potentially empty) scene.
        InstanceCount = 0;
        RangeCount    = 0;
        MaterialCount = 0;
        TotalTriangles = 0;
        TotalVertices  = 0;

        // Invalidate the monotonic frame guard: force the very next
        // BuildFromScene call to execute unconditionally, regardless of
        // whether the caller passes the same frameNumber as a previous call.
        m_LastBuiltFrameNumber = UINT64_MAX;

        // NOTE: GeometryPool (GlobalGeometryPool) caches are NOT cleared.
        // m_MeshRanges inside GeometryPool maps Mesh*→GeometryRange and is
        // a global write-once cache that persists across all scenes.
    }

} // namespace Ayaya
