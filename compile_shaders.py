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

# ── Shader variants: compile multiple SPIR-V binaries from one source via -D macros ──
# Key: relative path from SRC_DIR.  Value: list of variant configs.
# Each variant specifies #define macros and a suffix appended before .spv.
SHADER_VARIANTS = {
    "Shadow/cull_shadow.comp": [
        {"defines": {"USE_INDIRECT_COUNT": "1"}, "suffix": "_atomic"},
        {"defines": {}, "suffix": "_fixed"},  # no define → #ifdef USE_INDIRECT_COUNT is false
    ],
    # Hardware PCF fallback for MoltenVK without mutableComparisonSamplers
    "Deferred/deferred_lighting.frag": [
        {"defines": {"USE_HARDWARE_PCF": "1"}, "suffix": ""},       # default: sampler2DShadow
        {"defines": {}, "suffix": "_nohwpc"},                        # fallback: sampler2D + manual PCF
    ],
    "Debug/pbr_forward.frag": [
        {"defines": {"USE_HARDWARE_PCF": "1"}, "suffix": ""},       # default: sampler2DShadow
        {"defines": {}, "suffix": "_nohwpc"},                        # fallback: sampler2D + manual PCF
    ],
}

def compile_shader(src_path: Path, defines=None, variant_suffix=""):
    # 计算目标输出路径: 将 src 替换为 cache，并增加 variant_suffix + .spv 后缀
    relative_path = src_path.relative_to(SRC_DIR)
    stem = relative_path.stem  # filename without extension
    ext = relative_path.suffix  # .vert / .frag / .comp
    out_name = stem + variant_suffix + ext + ".spv"
    out_path = CACHE_DIR / relative_path.with_name(out_name)

    # 确保输出目录存在
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # 构建 glslc 参数
    cmd = [GLSLC]
    if defines:
        for key, val in defines.items():
            cmd.append(f"-D{key}={val}")
    cmd.append(str(src_path))
    cmd.append("-o")
    cmd.append(str(out_path))

    # 增量编译检查：如果输出文件比源文件新，跳过
    if out_path.exists():
        src_mtime = os.path.getmtime(src_path)
        out_mtime = os.path.getmtime(out_path)
        # Also check the script itself for variant changes
        script_mtime = os.path.getmtime(__file__)
        if src_mtime < out_mtime and script_mtime < out_mtime:
            return False

    # 执行编译命令
    label = relative_path.as_posix()
    if variant_suffix:
        label += f" [{variant_suffix.strip('_')}]"
    print(f"Compiling: {label} -> {out_path.name}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error compiling {label}:\n{result.stderr}")
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
            if src_path.suffix not in SHADER_EXTENSIONS:
                continue

            relative = (src_path.relative_to(SRC_DIR)).as_posix()

            # Check if this shader has variant configs
            if relative in SHADER_VARIANTS:
                for variant in SHADER_VARIANTS[relative]:
                    total_count += 1
                    if compile_shader(src_path, defines=variant["defines"], variant_suffix=variant["suffix"]):
                        compiled_count += 1
            else:
                total_count += 1
                if compile_shader(src_path):
                    compiled_count += 1

    end_time = time.time()
    print("---------------------------------------------")
    print(f"Done. Processed {compiled_count}/{total_count} shaders.")
    print(f"Time elapsed: {end_time - start_time:.2f}s")

if __name__ == "__main__":
    main()
