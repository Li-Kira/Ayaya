#include "ayapch.h"
#include "PlatformUtils.hpp"
#include <stdio.h>
#include <cstdio>

// ==========================================
// 引入 Windows 原生对话框头文件
// ==========================================
#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #include <commdlg.h>
    #include <shobjidl.h>
#endif

namespace Ayaya {

    std::string FileDialogs::OpenFile(const char* filter) {
        // ... (保持你原有的 OpenFile 代码不变) ...
#ifdef _WIN32
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        std::string filterStr(filter);
        for (char& c : filterStr) {
            if (c == '|') c = '\0';
        }

        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL; 
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        // c_str() 会在末尾自动补上一个 \0，加上前面替换的，正好满足 Windows 的双 \0 结尾要求！
        ofn.lpstrFilter = filterStr.c_str(); 
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        
        if (GetOpenFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
#else
        // macOS 实现保持不变...
        char buffer[1024];
        std::string result = "";
        FILE* pipe = popen("osascript -e 'POSIX path of (choose file with prompt \"Select Scene:\")' 2>/dev/null", "r");
        if (!pipe) return "";
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) { result += buffer; }
        pclose(pipe);
        if (!result.empty() && result[result.length()-1] == '\n') result.erase(result.length()-1);
        return result;
#endif
    }

    std::vector<std::string> FileDialogs::OpenFiles(const char* filter) {
        std::vector<std::string> results;
#ifdef _WIN32
        OPENFILENAMEA ofn;
        CHAR szFile[4096] = { 0 };
        std::string filterStr(filter);
        for (char& c : filterStr) if (c == '|') c = '\0';
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filterStr.c_str();
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) == TRUE) {
            const char* p = ofn.lpstrFile;
            std::string dir(p);
            p += dir.length() + 1;
            if (*p == 0) { results.push_back(dir); }  // single file
            else { while (*p) { std::string f(p); results.push_back(dir + "\\" + f); p += f.length() + 1; } }
        }
#else
        // macOS: write AppleScript to temp file to avoid quote-escaping nightmare
        char tmppath[] = "/tmp/ayaya_multiselect.scpt";
        {
            FILE* f = fopen(tmppath, "w");
            if (f) {
                fprintf(f, "set theFiles to choose file with multiple selections allowed with prompt \"Select Texture Files:\"\n");
                fprintf(f, "set out to \"\"\n");
                fprintf(f, "repeat with f in theFiles\n");
                fprintf(f, "  set out to out & POSIX path of f & \"\\n\"\n");
                fprintf(f, "end repeat\n");
                fprintf(f, "return out\n");
                fclose(f);
            }
        }
        char buffer[8192];
        std::string raw;
        FILE* pipe = popen("osascript /tmp/ayaya_multiselect.scpt 2>/dev/null", "r");
        if (!pipe) return results;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) raw += buffer;
        pclose(pipe);
        size_t pos = 0;
        while (pos < raw.length()) {
            size_t nl = raw.find('\n', pos);
            std::string line = (nl == std::string::npos) ? raw.substr(pos) : raw.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) results.push_back(line);
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
#endif
        return results;
    }

    std::string FileDialogs::SaveFile(const char* filter, const std::string& defaultName) {
        // ... (保持你原有的 SaveFile 代码不变) ...
#ifdef _WIN32
       OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        strncpy(szFile, defaultName.c_str(), sizeof(szFile) - 1);

        // ==========================================
        // 【核心修复】：同样在这里替换 '|' 为 '\0'
        // ==========================================
        std::string filterStr(filter);
        for (char& c : filterStr) {
            if (c == '|') c = '\0';
        }

        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filterStr.c_str(); // 使用转换后的字符串
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        
        if (GetSaveFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
        return std::string();
#else
        // macOS 实现保持不变...
        char buffer[1024];
        std::string result = "";
        std::string command = "osascript -e 'POSIX path of (choose file name with prompt \"Save Scene As:\" default name \"" + defaultName + "\")' 2>/dev/null";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return "";
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) { result += buffer; }
        pclose(pipe);
        if (!result.empty() && result[result.length()-1] == '\n') result.erase(result.length()-1);
        return result;
#endif
    }

    void FileDialogs::ShowInFileExplorer(const std::string& path) {
        if (path.empty()) return;
#ifdef _WIN32
        std::string cmd = "/select,\"" + path + "\"";
        ShellExecuteA(NULL, "open", "explorer.exe", cmd.c_str(), NULL, SW_SHOW);
#else
        std::string cmd = "open -R '" + path + "' 2>/dev/null";
        system(cmd.c_str());
#endif
    }

    void FileDialogs::OpenInFileExplorer(const std::string& folderPath) {
        if (folderPath.empty()) return;
#ifdef _WIN32
        ShellExecuteA(NULL, "open", folderPath.c_str(), NULL, NULL, SW_SHOW);
#else
        std::string cmd = "open '" + folderPath + "' 2>/dev/null";
        system(cmd.c_str());
#endif
    }

    // =====================================================================
    // 【新增】：实现跨平台的选择文件夹对话框
    // =====================================================================
    std::string FileDialogs::OpenFolder() {
#ifdef _WIN32
        std::string result = "";
        
        // 1. 初始化 COM 库 (带安全防护，防止别的线程已经初始化过)
        bool comInit = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

        IFileOpenDialog* pFileOpen;
        // 2. 创建现代的 FileOpenDialog 对象
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
        
        if (SUCCEEDED(hr)) {
            DWORD dwOptions;
            if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                // 3. 【核心】：设置标志位，让它只能选文件夹，不能选文件！
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
            }
            
            // 4. 显示对话框给用户
            if (SUCCEEDED(pFileOpen->Show(NULL))) {
                IShellItem* pItem;
                if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                    PWSTR pszFilePath;
                    // 5. 获取用户选中的文件夹路径
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                        // 6. 将 Windows 的宽字符 (wchar_t) 转换为标准的 UTF-8 std::string
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                        if (size_needed > 0) {
                            std::string strTo(size_needed, 0);
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &strTo[0], size_needed, NULL, NULL);
                            // 移除 API 自动在末尾添加的 '\0' 结束符
                            if (!strTo.empty() && strTo.back() == '\0') {
                                strTo.pop_back();
                            }
                            result = strTo;
                        }
                        CoTaskMemFree(pszFilePath); // 释放 COM 内存
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
        
        if (comInit) {
            CoUninitialize(); // 打扫战场
        }
        
        return result;
#else
        // ==========================================
        // macOS 实现：利用 AppleScript 调出选择文件夹窗口
        // ==========================================
        char buffer[1024];
        std::string result = "";
        
        FILE* pipe = popen("osascript -e 'POSIX path of (choose folder with prompt \"Select Project Location:\")' 2>/dev/null", "r");
        if (!pipe) return "";
        
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        // 移除换行符
        if (!result.empty() && result[result.length()-1] == '\n') {
            result.erase(result.length()-1);
        }
        return result;
#endif
    }
}