#!/usr/bin/env python3
"""Compiles .hlsl files in a project to SPIR-V via Microsoft DXC.
Usage: python3 compile_hlsl.py <project_asset_dir>
Example: python3 compile_hlsl.py assets/AyayaProject/Graphics/testSRP/Assets

Output: <project_dir>/Shaders/Cache/<name>.<vert|frag|comp>.spv
"""
import sys, os, subprocess, re
from pathlib import Path

DXC = "/usr/local/bin/dxc"

def compile_hlsl(project_dir):
    src_dir = Path(project_dir) / "Shaders"
    if not src_dir.exists():
        print(f"[HLSL] No Shaders/ directory found in {project_dir}")
        return 1

    cache_dir = src_dir / "Cache"
    cache_dir.mkdir(parents=True, exist_ok=True)

    hlsl_files = list(src_dir.glob("*.hlsl"))
    if not hlsl_files:
        print(f"[HLSL] No .hlsl files found in {src_dir}")
        return 0

    ok_count = 0
    fail_count = 0

    for hlsl_file in hlsl_files:
        content = hlsl_file.read_text(encoding='utf-8')
        stem = hlsl_file.stem

        # Only compile stages whose entry points exist in source
        stages = []
        if re.search(r'\bVSMain\b', content):
            stages.append(("vert", "VSMain", "vs_6_0"))
        if re.search(r'\bPSMain\b', content):
            stages.append(("frag", "PSMain", "ps_6_0"))
        if re.search(r'\bCSMain\b', content):
            stages.append(("comp", "CSMain", "cs_6_0"))

        if not stages:
            print(f"  SKIP: {stem}.hlsl — no VSMain/PSMain/CSMain entry point")
            continue

        for stage, entry, profile in stages:
            out_path = cache_dir / f"{stem}.{stage}.spv"
            cmd = [DXC, "-spirv", "-T", profile, "-E", entry,
                   str(hlsl_file), "-Fo", str(out_path),
                   "-fvk-use-dx-layout"]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode == 0:
                print(f"  OK: {stem}.{stage}.spv")
                ok_count += 1
            else:
                # Print first 300 chars of error
                err = result.stderr.strip()
                if not err:
                    err = result.stdout.strip()
                print(f"  FAIL: {stem}.{stage} — {err[:300]}")
                fail_count += 1

    print(f"[HLSL] Done. {ok_count} OK, {fail_count} FAIL")
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 compile_hlsl.py <project_asset_dir>")
        sys.exit(1)
    sys.exit(compile_hlsl(sys.argv[1]))
