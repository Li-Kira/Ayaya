#include "ayapch.h"
#include "PlatformUtils.hpp"
#include <stdio.h>

// ==========================================
// 引入 Windows 原生对话框头文件
// ==========================================
#ifdef _WIN32
    #include <windows.h>
    #include <commdlg.h>
#endif

namespace Ayaya {

    std::string FileDialogs::OpenFile(const char* filter) {
#ifdef _WIN32
        // ==========================================
        // Windows 实现
        // ==========================================
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL; // 依附的父窗口句柄，NULL 也可以正常工作
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        // OFN_NOCHANGEDIR 极其重要！防止 Windows 偷偷改变引擎的相对路径起始点！
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
#else
        // ==========================================
        // macOS 实现 (保持你原来的代码)
        // ==========================================
        char buffer[1024];
        std::string result = "";
        
        FILE* pipe = popen("osascript -e 'POSIX path of (choose file with prompt \"Select Scene:\")' 2>/dev/null", "r");
        if (!pipe) return "";
        
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        if (!result.empty() && result[result.length()-1] == '\n') {
            result.erase(result.length()-1);
        }
        return result;
#endif
    }

    std::string FileDialogs::SaveFile(const char* filter, const std::string& defaultName) {
#ifdef _WIN32
        // ==========================================
        // Windows 实现
        // ==========================================
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        
        // 填入默认的文件名
        strncpy(szFile, defaultName.c_str(), sizeof(szFile) - 1);
        
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        // 加上 OFN_OVERWRITEPROMPT，覆盖文件时会有确认弹窗
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (GetSaveFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
#else
        // ==========================================
        // macOS 实现 (保持你原来的代码)
        // ==========================================
        char buffer[1024];
        std::string result = "";
        
        std::string command = "osascript -e 'POSIX path of (choose file name with prompt \"Save Scene As:\" default name \"" + defaultName + "\")' 2>/dev/null";
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return "";
        
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        if (!result.empty() && result[result.length()-1] == '\n') {
            result.erase(result.length()-1);
        }
        return result;
#endif
    }

}