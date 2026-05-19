import os
from pathlib import Path

def generate_tree(data_path, prefix=""):
    """
    递归遍历目录并生成树状结构文本，跳过 vendor 目录的内部遍历
    """
    # 过滤掉不需要显示的隐藏文件（如 .DS_Store）
    items = [item for item in data_path.iterdir() if not item.name.startswith('.')]
    # 排序：文件夹在前，文件在后，并按字母顺序
    items = sorted(items, key=lambda x: (x.is_file(), x.name.lower()))
    
    for i, item in enumerate(items):
        is_last = (i == len(items) - 1)
        connector = "└── " if is_last else "├── "
        
        print(f"{prefix}{connector}{item.name}")
        
        # 如果是目录且名字不是 'vendor'，则递归进入
        if item.is_dir():
            if item.name.lower() == "vendor" or item.name.lower() == "build" or item.name.lower() == "assets" or item.name.lower() == "documents":
                # 如果是 vendor 目录，打印一行提示表示已跳过
                skipped_prefix = prefix + ("    " if is_last else "│   ")
                print(f"{skipped_prefix}└── (skipped contents)")
                continue
            
            new_prefix = prefix + ("    " if is_last else "│   ")
            generate_tree(item, new_prefix)

if __name__ == "__main__":
    current_dir = Path(".")
    print(f"Directory Structure for: {current_dir.absolute()}")
    generate_tree(current_dir)