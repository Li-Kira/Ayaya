#pragma once
#include <string>
#include <vector>

namespace Ayaya {

    class FileDialogs {
    public:
        // 呼出打开文件窗口。如果用户点击了取消，则返回空字符串
        static std::string OpenFile(const char* filter);
        static std::vector<std::string> OpenFiles(const char* filter);

        // 呼出保存文件窗口。如果用户点击了取消，则返回空字符串
        static std::string SaveFile(const char* filter, const std::string& defaultName = "Untitled.ayaya");

        // 选择文件夹对话框
        static std::string OpenFolder();

        // 在系统文件管理器中定位并高亮指定路径
        static void ShowInFileExplorer(const std::string& path);

        // 在系统文件管理器中打开文件夹 (不选中特定文件)
        static void OpenInFileExplorer(const std::string& folderPath);
    };

}