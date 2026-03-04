#include "ayapch.h"
#include "PlatformUtils.hpp"
#include <stdio.h>

namespace Ayaya {

    // macOS 专用的对话框实现 (通过调用系统的 osascript 呼出原生窗口)
    std::string FileDialogs::OpenFile(const char* filter) {
        char buffer[1024];
        std::string result = "";
        
        // 调用 macOS 原生文件选择器。
        // 2>/dev/null 用于屏蔽用户点击“取消”时的系统报错输出。
        FILE* pipe = popen("osascript -e 'POSIX path of (choose file with prompt \"Select Scene:\")' 2>/dev/null", "r");
        if (!pipe) return "";
        
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        // 去除末尾的换行符
        if (!result.empty() && result[result.length()-1] == '\n') {
            result.erase(result.length()-1);
        }
        return result;
    }

    std::string FileDialogs::SaveFile(const char* filter, const std::string& defaultName) {
        char buffer[1024];
        std::string result = "";
        
        // --- 核心修改：动态拼装 osascript 命令，插入 defaultName ---
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
    }

}