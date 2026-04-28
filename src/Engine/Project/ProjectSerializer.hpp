#pragma once

#include <memory>
#include <string>
#include <filesystem>

namespace Ayaya {

    // 前向声明 Project 类，避免头文件循环依赖
    class Project;

    class ProjectSerializer {
    public:
        // 构造时传入需要被序列化或反序列化的 Project 实例
        ProjectSerializer(std::shared_ptr<Project> project);

        // 将当前 Project 的配置保存到指定的物理路径 (.ayaproj 文件)
        bool Serialize(const std::filesystem::path& filepath);

        // 从指定的物理路径读取配置，并覆盖到当前的 Project 实例中
        bool Deserialize(const std::filesystem::path& filepath);

    private:
        std::shared_ptr<Project> m_Project;
    };

}