#include "ayapch.h"
#include "Project.hpp"
#include "ProjectSerializer.hpp"
#include "Engine/Core/VFS.hpp"

namespace Ayaya {

    std::shared_ptr<Project> Project::New() {
        s_ActiveProject = std::make_shared<Project>();
        s_ActiveProject->m_ProjectDirectory = std::filesystem::current_path();
        
        // 挂载项目根目录
        VFS::Mount("project", s_ActiveProject->m_ProjectDirectory);
        // 挂载引擎自带资源目录
        VFS::Mount("engine", std::filesystem::current_path() / "assets/Editor");
        
        return s_ActiveProject;
    }

    std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        
        if (serializer.Deserialize(path)) {
            // .ayaproj 所在的目录就是项目根目录
            project->m_ProjectDirectory = path.parent_path();
            s_ActiveProject = project;

            // 【双沙盒挂载】
            // 1. 项目挂载点：指向用户的游戏根目录
            VFS::Mount("project", s_ActiveProject->m_ProjectDirectory);
            
            // 2. 引擎挂载点：指向引擎的公共资源目录
            VFS::Mount("engine", std::filesystem::current_path() / "assets/Editor");

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