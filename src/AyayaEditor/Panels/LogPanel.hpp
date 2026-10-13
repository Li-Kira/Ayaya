#pragma once

#include <cstdint>
#include <string>

namespace Ayaya {

    // ==========================================
    // LogPanel — 编辑器输出日志面板 (Output Log)
    // 消费 Log::GetRecentEntries() 的内存环形缓冲。
    // 支持：级别过滤、搜索、复制当前行/全部、Word Wrap、折叠重复。
    // ==========================================
    class LogPanel {
    public:
        void RenderContent();  // 不 Begin 窗口，只画内容 (供底部停靠面板复用)

    private:
        // 级别过滤
        bool m_ShowTrace = false;
        bool m_ShowInfo  = true;
        bool m_ShowWarn  = true;
        bool m_ShowError = true;

        // 搜索 / 显示选项
        char m_SearchFilter[256] = "";
        bool m_WordWrap = true;
        bool m_CollapseDuplicates = false;
        bool m_AutoScroll = true;

        // 选中行 (跨帧稳定，用 LogEntry::sequence 标识)
        uint64_t    m_SelectedSeq = UINT64_MAX;  // 哨兵 = 无选中
        std::string m_SelectedText;              // 选中行全文，供 Ctrl+C 复制
    };

} // namespace Ayaya
