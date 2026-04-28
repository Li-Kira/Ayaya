#include "ayapch.h"
#include "Project.hpp"
#include "ProjectSerializer.hpp"

namespace Ayaya {

    // 【新增】：创建一个空的默认项目
    std::shared_ptr<Project> Project::New() {
        s_ActiveProject = std::make_shared<Project>();
        // 默认将项目目录设为当前运行目录
        s_ActiveProject->m_ProjectDirectory = std::filesystem::current_path();
        return s_ActiveProject;
    }

    std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
        std::shared_ptr<Project> project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        
        if (serializer.Deserialize(path)) {
            // 【最核心的一步】：将 .ayaproj 文件所在的文件夹，设为项目的根物理路径！
            project->m_ProjectDirectory = path.parent_path();
            
            s_ActiveProject = project;
            return s_ActiveProject;
        }
        return nullptr;
    }

    bool Project::SaveActive(const std::filesystem::path& path) {
        ProjectSerializer serializer(s_ActiveProject);
        if (serializer.Serialize(path)) {
            s_ActiveProject->m_ProjectDirectory = path.parent_path();
            return true;
        }
        return false;
    }
}