#pragma once
#include <string>

namespace Ayaya {

    class FileDialogs {
    public:
        // 呼出打开文件窗口。如果用户点击了取消，则返回空字符串
        static std::string OpenFile(const char* filter);
        
        // 呼出保存文件窗口。如果用户点击了取消，则返回空字符串
        // --- 修改：增加 defaultName 参数 ---
        static std::string SaveFile(const char* filter, const std::string& defaultName = "Untitled.ayaya");
    };

}