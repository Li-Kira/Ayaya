#!/usr/bin/env python3
"""Compiles .hlsl files in a project to SPIR-V via Microsoft DXC.
Usage: python3 compile_hlsl.py <project_asset_dir>

Entry point is always 'main' (matches engine VulkanPipeline expectation).
Use #ifdef VERTEX_SHADER / #else in HLSL to separate VS and PS code.
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

        # Detect stages from shader content (entry point is always 'main')
        stages = []
        # VS: SV_VertexID or gl_VertexIndex or VERTEX_SHADER ifdef
        if re.search(r'SV_VertexID|gl_VertexIndex|VERTEX_SHADER', content):
            stages.append(("vert", "main", "vs_6_0", {"VERTEX_SHADER": "1"}))
        # PS: SV_TARGET or Texture2D (and not VERTEX_SHADER-compiled VS section)
        if re.search(r'SV_TARGET|Texture2D\b', content):
            stages.append(("frag", "main", "ps_6_0", {}))
        # CS: numthreads or SV_DispatchThreadID
        if re.search(r'numthreads|SV_DispatchThreadID|gl_GlobalInvocationID', content):
            stages.append(("comp", "main", "cs_6_0", {}))

        if not stages:
            print(f"  SKIP: {stem}.hlsl — no recognizable shader stage")
            continue

        for stage, entry, profile, defines in stages:
            out_path = cache_dir / f"{stem}.{stage}.spv"
            tmp_path = cache_dir / f"{stem}.{stage}.spv.tmp"
            include_dir = Path(__file__).parent / "assets/Editor/shaders/src/vulkan"
            cmd = [DXC, "-spirv", "-T", profile, "-E", entry,
                   "-I", str(include_dir),
                   str(hlsl_file), "-Fo", str(tmp_path)]
            for dk, dv in defines.items():
                cmd.extend(["-D", f"{dk}={dv}"])
            cmd.append("-fvk-use-dx-layout")
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode == 0:
                tmp_path.replace(out_path)  # atomic: only overwrite on success
                print(f"  OK: {stem}.{stage}.spv")
                ok_count += 1
            else:
                tmp_path.unlink(missing_ok=True)  # clean up broken temp file
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
