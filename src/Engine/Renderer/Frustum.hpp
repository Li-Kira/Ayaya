#pragma once
#include <glm/glm.hpp>
#include "Mesh.hpp"

namespace Ayaya {

    struct Plane {
        glm::vec3 Normal = { 0.f, 1.f, 0.f };
        float Distance = 0.f;

        void Normalize() {
            float mag = glm::length(Normal);
            Normal /= mag;
            Distance /= mag;
        }

        // 计算点到平面的有向距离
        float GetSignedDistanceToPlane(const glm::vec3& point) const {
            return glm::dot(Normal, point) + Distance;
        }
    };

    class Frustum {
    public:
        Plane Planes[6];

        // 核心数学：从 ViewProjection 矩阵提取 6 个视锥体平面 (Gribb-Hartmann 方法)
        Frustum(const glm::mat4& viewProjection) {
            // Left
            Planes[0].Normal.x = viewProjection[0][3] + viewProjection[0][0];
            Planes[0].Normal.y = viewProjection[1][3] + viewProjection[1][0];
            Planes[0].Normal.z = viewProjection[2][3] + viewProjection[2][0];
            Planes[0].Distance = viewProjection[3][3] + viewProjection[3][0];

            // Right
            Planes[1].Normal.x = viewProjection[0][3] - viewProjection[0][0];
            Planes[1].Normal.y = viewProjection[1][3] - viewProjection[1][0];
            Planes[1].Normal.z = viewProjection[2][3] - viewProjection[2][0];
            Planes[1].Distance = viewProjection[3][3] - viewProjection[3][0];

            // Bottom
            Planes[2].Normal.x = viewProjection[0][3] + viewProjection[0][1];
            Planes[2].Normal.y = viewProjection[1][3] + viewProjection[1][1];
            Planes[2].Normal.z = viewProjection[2][3] + viewProjection[2][1];
            Planes[2].Distance = viewProjection[3][3] + viewProjection[3][1];

            // Top
            Planes[3].Normal.x = viewProjection[0][3] - viewProjection[0][1];
            Planes[3].Normal.y = viewProjection[1][3] - viewProjection[1][1];
            Planes[3].Normal.z = viewProjection[2][3] - viewProjection[2][1];
            Planes[3].Distance = viewProjection[3][3] - viewProjection[3][1];

            // Near
            Planes[4].Normal.x = viewProjection[0][3] + viewProjection[0][2];
            Planes[4].Normal.y = viewProjection[1][3] + viewProjection[1][2];
            Planes[4].Normal.z = viewProjection[2][3] + viewProjection[2][2];
            Planes[4].Distance = viewProjection[3][3] + viewProjection[3][2];

            // Far
            Planes[5].Normal.x = viewProjection[0][3] - viewProjection[0][2];
            Planes[5].Normal.y = viewProjection[1][3] - viewProjection[1][2];
            Planes[5].Normal.z = viewProjection[2][3] - viewProjection[2][2];
            Planes[5].Distance = viewProjection[3][3] - viewProjection[3][2];

            for (int i = 0; i < 6; i++) {
                Planes[i].Normalize();
            }
        }

        // ==========================================
        // 核心算法：判断转换到世界空间的 AABB 是否在视锥体内
        // ==========================================
        bool IsBoxVisible(const AABB& aabb, const glm::mat4& transform) const {
            // 获取全局坐标下的缩放和位移（为简化计算，假设只包含位移和等比缩放，严格来说应该变换 8 个顶点）
            glm::vec3 globalCenter = transform * glm::vec4((aabb.Min + aabb.Max) * 0.5f, 1.0f);
            
            // 提取缩放大小 (取三轴最大值)
            glm::vec3 right = glm::vec3(transform[0][0], transform[0][1], transform[0][2]);
            glm::vec3 up = glm::vec3(transform[1][0], transform[1][1], transform[1][2]);
            glm::vec3 forward = glm::vec3(transform[2][0], transform[2][1], transform[2][2]);
            
            glm::vec3 extents = (aabb.Max - aabb.Min) * 0.5f;
            
            // 粗略的将 AABB 转换为边界球 (Bounding Sphere) 半径测试，这比严格的 8 顶点 AABB 测试性能快得多，且足够用
            float maxScale = glm::max(glm::length(right), glm::max(glm::length(up), glm::length(forward)));
            float radius = glm::length(extents) * maxScale;

            for (int i = 0; i < 6; i++) {
                // 如果球心距离平面的距离小于负的半径，说明整个球体都在这个平面的“外面”
                if (Planes[i].GetSignedDistanceToPlane(globalCenter) < -radius) {
                    return false; // 直接剔除！
                }
            }
            return true;
        }
    };
}