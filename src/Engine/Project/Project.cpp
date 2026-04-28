#include "ayapch.h"
#include "Project.hpp"
#include "ProjectSerializer.hpp"

namespace Ayaya {

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