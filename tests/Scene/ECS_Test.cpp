#include <entt/entt.hpp>
#include <gtest/gtest.h>
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"
#include <glm/gtc/matrix_transform.hpp>
using namespace Ayaya;

TEST(SceneTest, A_Create) {
    auto scene = std::make_shared<Scene>();
    Entity e = scene->CreateEntity("Test");
    EXPECT_TRUE(e.GetEntityHandle() != entt::null);
}

TEST(SceneTest, B_Transform) {
    auto scene = std::make_shared<Scene>();
    auto& t = scene->CreateEntity().GetComponent<TransformComponent>();
    EXPECT_FLOAT_EQ(t.Translation.x, 0.0f);
}

TEST(SceneTest, C_Lookup) {
    auto scene = std::make_shared<Scene>();
    Entity e = scene->CreateEntity();
    Entity found = scene->GetEntityByUUID(e.GetComponent<IDComponent>().ID);
    EXPECT_TRUE(found.GetEntityHandle() != entt::null);
}

TEST(SceneTest, D_AddComp) {
    auto scene = std::make_shared<Scene>();
    Entity e = scene->CreateEntity();
    e.AddComponent<CameraComponent>();
    EXPECT_TRUE(e.HasComponent<CameraComponent>());
}

TEST(SceneTest, E_RemoveComp) {
    auto scene = std::make_shared<Scene>();
    Entity e = scene->CreateEntity();
    e.AddComponent<CameraComponent>();
    e.RemoveComponent<CameraComponent>();
    bool has = e.HasComponent<CameraComponent>();
    EXPECT_FALSE(has);
}

TEST(SceneTest, F_Destroy) {
    auto scene = std::make_shared<Scene>();
    Entity e = scene->CreateEntity();
    UUID id = e.GetComponent<IDComponent>().ID;
    scene->DestroyEntity(e);
    entt::entity result = scene->GetEntityByUUID(id).GetEntityHandle();
    entt::entity nil = entt::null;
    EXPECT_EQ(result, nil);
}

TEST(SceneTest, G_SetParent) {
    auto scene = std::make_shared<Scene>();
    auto p = scene->CreateEntity();
    auto c = scene->CreateEntity();
    c.SetParent(p);
    EXPECT_EQ(c.GetComponent<RelationshipComponent>().Parent, p.GetEntityHandle());
}

TEST(SceneTest, H_WorldXform) {
    auto scene = std::make_shared<Scene>();
    auto p = scene->CreateEntity();
    auto c = scene->CreateEntity();
    c.SetParent(p);
    p.GetComponent<TransformComponent>().Translation = {10, 0, 0};
    c.GetComponent<TransformComponent>().Translation  = {0, 5, 0};
    glm::mat4 w = c.GetWorldTransform();
    EXPECT_FLOAT_EQ(w[3][0], 10.0f);
    EXPECT_FLOAT_EQ(w[3][1], 5.0f);
}
