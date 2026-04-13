import os
import subprocess
import time
import platform
from pathlib import Path

# ================= 配置区 =================
# 根据当前操作系统自动选择 glslc 可执行文件名
if platform.system() == "Windows":
    GLSLC = "glslc.exe"
else:
    GLSLC = "glslc" # Mac 和 Linux 使用这个

# 💡 提示：如果你的 Mac 终端里直接敲 glslc 找不到命令（说明没配环境变量），
# 可以把上面那行换成你的 Vulkan SDK 绝对路径，例如：
# GLSLC = "/Users/likirac/VulkanSDK/1.3.268.0/macOS/bin/glslc"

# 匹配我们在 C++ 中定义的路径结构
BASE_DIR = Path("assets/Editor/shaders")
SRC_DIR = BASE_DIR / "src/vulkan"
CACHE_DIR = BASE_DIR / "cache/vulkan"

# 支持的 Shader 后缀
SHADER_EXTENSIONS = [".vert", ".frag", ".comp", ".geom", ".tesc", ".tese"]

def compile_shader(src_path: Path):
    # 计算目标输出路径: 将 src 替换为 cache，并增加 .spv 后缀
    relative_path = src_path.relative_to(SRC_DIR)
    out_path = CACHE_DIR / relative_path.with_suffix(src_path.suffix + ".spv")

    # 检查是否需要增量编译
    if out_path.exists():
        src_mtime = os.path.getmtime(src_path)
        out_mtime = os.path.getmtime(out_path)
        if src_mtime < out_mtime:
            # 源码没改动，跳过
            return False

    # 确保输出目录存在
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # 执行编译命令
    print(f"Compiling: {relative_path} -> {out_path.name}")
    try:
        result = subprocess.run([GLSLC, str(src_path), "-o", str(out_path)], 
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error compiling {src_path}:\n{result.stderr}")
            return False
    except Exception as e:
        print(f"Failed to run glslc: {e}\n(请确保 '{GLSLC}' 已加入系统环境变量，或在脚本中配置绝对路径)")
        return False
    
    return True

def main():
    if not SRC_DIR.exists():
        print(f"Error: Source directory {SRC_DIR} does not exist!")
        return

    print("--- Ayaya Shader Compiler (Vulkan SPIR-V) ---")
    start_time = time.time()
    compiled_count = 0
    total_count = 0

    # 递归遍历所有文件
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            src_path = Path(root) / file
            if src_path.suffix in SHADER_EXTENSIONS:
                total_count += 1
                if compile_shader(src_path):
                    compiled_count += 1

    end_time = time.time()
    print("---------------------------------------------")
    print(f"Done. Processed {total_count} shaders.")
    print(f"Compiled {compiled_count} modified shaders.")
    print(f"Time elapsed: {end_time - start_time:.2f}s")

if __name__ == "__main__":
    main()