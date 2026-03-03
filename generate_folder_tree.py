import os
from pathlib import Path

def generate_tree(data_path, prefix=""):
    """
    递归遍历目录并生成树状结构文本
    """
    # 获取目录下所有文件和文件夹，并排序（保持输出稳定）
    items = sorted(list(data_path.iterdir()), key=lambda x: (x.is_file(), x.name.lower()))
    
    for i, item in enumerate(items):
        # 判断是否为最后一个元素，决定使用的符号
        is_last = (i == len(items) - 1)
        connector = "└── " if is_last else "├── "
        
        print(f"{prefix}{connector}{item.name}")
        
        # 如果是目录，则递归进入
        if item.is_dir():
            # 最后一个元素的子节点不需要垂直线
            new_prefix = prefix + ("    " if is_last else "│   ")
            generate_tree(item, new_prefix)

if __name__ == "__main__":
    # Path(".") 代表当前运行脚本的目录
    current_dir = Path(".")
    print(f"Directory Structure for: {current_dir.absolute()}")
    generate_tree(current_dir)