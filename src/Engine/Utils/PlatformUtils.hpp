#pragma once
#include <string>

namespace Ayaya {

    class FileDialogs {
    public:
        // 呼出打开文件窗口。如果用户点击了取消，则返回空字符串
        static std::string OpenFile(const char* filter);
        
        // 呼出保存文件窗口。如果用户点击了取消，则返回空字符串
        static std::string SaveFile(const char* filter, const std::string& defaultName = "Untitled.ayaya");

        // ==========================================
        // 【新增】：专门用于选择文件夹的弹窗
        // ==========================================
        static std::string OpenFolder();
    };

}