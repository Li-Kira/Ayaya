#pragma once
#include <memory>
#include <string>

namespace Ayaya {

    class Command {
    public:
        virtual ~Command() = default;
        
        virtual void Execute() = 0; // 执行（重做）
        virtual void Undo() = 0;    // 撤销
        
        // 用于在 UI 上显示“撤回了什么”（可选，但极大提升体验）
        virtual std::string GetName() const { return "Unknown Command"; }
        
        // 核心优化：用于合并连续操作（比如拖动 Slider 产生的几百个微小变化）
        virtual bool MergeWith(Command* other) { return false; }
    };

}