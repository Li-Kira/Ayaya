#!/usr/bin/env python3
"""
批量替换 AyayaProject 中引用的引擎内置资产 UUID。

1. 读取每个项目的 AssetRegistry.yaml，找到引擎虚拟路径对应的旧 UUID
2. 在项目所有文件中将旧 UUID 替换为 EngineAssets.yaml 中定义的新 UUID
3. 更新 AssetRegistry.yaml 本身
"""

import os
import sys
import yaml
import shutil
from pathlib import Path

# 引擎资产的新 UUID（与 assets/Editor/EngineAssets.yaml 保持一致）
ENGINE_ASSETS = {
    "Primitive::Cube":                             16140901000000000001,
    "Primitive::Sphere":                           16140901000000000002,
    "Primitive::Plane":                            16140901000000000003,
    "engine://Editor/materials/DefaultPBR.mat":    16140901000000000004,
    "engine://materials/DefaultPBR.mat":           16140901000000000004,  # 兼容旧路径
    "engine://Editor/textures/skybox/hdr/newport_loft.hdr": 16140901000000000005,
    "engine://Editor/textures/skybox/skybox_01/sky.cube":    16140901000000000006,
}

# 需要扫描替换的文件扩展名
SCAN_EXTS = {".ayaya", ".mat", ".yaml"}

def find_ayaproject_root():
    """自动探测 AyayaProject 根目录"""
    candidates = [
        Path(__file__).parent / "assets" / "AyayaProject",
        Path.cwd() / "assets" / "AyayaProject",
    ]
    for c in candidates:
        if c.exists():
            return c.resolve()
    return None

def read_registry(registry_path):
    """读取 AssetRegistry.yaml，返回 {virtual_path: uuid}"""
    mapping = {}
    try:
        with open(registry_path, "r") as f:
            data = yaml.safe_load(f)
        entries = data.get("AssetRegistry", [])
        for entry in entries:
            vp = entry.get("VirtualPath", "")
            uid = entry.get("Handle", 0)
            if vp and uid:
                mapping[vp] = int(uid)
    except Exception as e:
        print(f"  ⚠ 读取 {registry_path} 失败: {e}")
    return mapping

def find_engine_uuids(registry_mapping):
    """从注册表映射中找出引擎资产的旧 UUID"""
    replacements = {}
    for vpath, new_uuid in ENGINE_ASSETS.items():
        old_uuid = registry_mapping.get(vpath)
        if old_uuid and old_uuid != new_uuid:
            replacements[str(old_uuid)] = str(new_uuid)
    return replacements

def replace_in_file(filepath, replacements):
    """在单个文件中执行 UUID 替换"""
    try:
        with open(filepath, "r") as f:
            content = f.read()

        modified = False
        for old, new in replacements.items():
            if old in content:
                content = content.replace(old, new)
                modified = True

        if modified:
            bak = str(filepath) + ".bak"
            shutil.copy2(filepath, bak)
            with open(filepath, "w") as f:
                f.write(content)
            os.remove(bak)
            return True
    except Exception as e:
        print(f"  ✗ {filepath}: {e}")
    return False

def migrate_project(project_dir):
    """迁移单个项目目录"""
    registry_path = project_dir / "AssetRegistry.yaml"
    if not registry_path.exists():
        print(f"  ⊘ 未找到 AssetRegistry.yaml，跳过")
        return

    registry_mapping = read_registry(registry_path)
    replacements = find_engine_uuids(registry_mapping)

    if not replacements:
        print(f"  ✓ 已是最新 UUID，无需迁移")
        return

    print(f"  替换映射:")
    for old, new in replacements.items():
        print(f"    {old} → {new}")

    count = 0
    for root, dirs, files in os.walk(project_dir):
        # 跳过备份文件和 .meta 文件
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for fname in files:
            ext = os.path.splitext(fname)[1].lower()
            if ext in SCAN_EXTS or fname == "AssetRegistry.yaml":
                fpath = Path(root) / fname
                if str(fpath) == str(registry_path):
                    continue  # 最后单独处理
                if replace_in_file(fpath, replacements):
                    count += 1

    # 最后更新 AssetRegistry.yaml
    if replace_in_file(registry_path, replacements):
        count += 1

    print(f"  ✓ 完成: {count} 个文件已更新")

    # 检查孤儿 UUID：场景引用了但不在注册表和 .meta 中的 UUID
    check_orphan_uuids(project_dir)


def check_orphan_uuids(project_dir):
    """扫描 .ayaya 文件中引用的 UUID，检测孤儿引用"""
    import re

    # 收集所有已知 UUID（从 .meta 文件 + AssetRegistry.yaml）
    known_uuids = set()
    for root, dirs, files in os.walk(project_dir):
        for fname in files:
            if fname.endswith(".meta"):
                try:
                    with open(os.path.join(root, fname)) as f:
                        data = yaml.safe_load(f)
                        known_uuids.add(str(data.get("uuid", 0)))
                except:
                    pass

    registry_path = project_dir / "AssetRegistry.yaml"
    if registry_path.exists():
        try:
            with open(registry_path) as f:
                data = yaml.safe_load(f)
            for entry in data.get("AssetRegistry", []):
                known_uuids.add(str(entry.get("Handle", 0)))
        except:
            pass

    # 添加引擎 UUID
    for uid in ENGINE_ASSETS.values():
        known_uuids.add(str(uid))

    # 扫描 .ayaya 文件中的 Handle 引用
    uuid_pattern = re.compile(r'(?:Model|Material|Texture|Equirectangular|Cubemap|Script)Handle:\s*(\d+)')
    for root, dirs, files in os.walk(project_dir):
        for fname in files:
            if fname.endswith(".ayaya"):
                fpath = os.path.join(root, fname)
                with open(fpath) as f:
                    content = f.read()
                for match in uuid_pattern.finditer(content):
                    uid = match.group(1)
                    if uid == "0":
                        continue
                    if uid not in known_uuids:
                        rel = os.path.relpath(fpath, project_dir)
                        print(f"  ⚠ 孤儿 UUID: {uid} ({match.group(1)}) 在 {rel}")


def main():
    root = find_ayaproject_root()
    if not root:
        print("错误: 找不到 assets/AyayaProject 目录")
        print("请从项目根目录运行此脚本")
        sys.exit(1)

    print(f"AyayaProject 根目录: {root}")
    print(f"引擎新 UUID:")
    for vp, uid in ENGINE_ASSETS.items():
        print(f"  {uid} → {vp}")
    print()

    for project_dir in sorted(root.iterdir()):
        if project_dir.is_dir():
            print(f"处理: {project_dir.name}")
            migrate_project(project_dir / "Assets")
            print()

    print("全部完成。")

if __name__ == "__main__":
    main()
