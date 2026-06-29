#!/usr/bin/env python3
"""SPIR-V shader reflection via spirv-cross --dump-resources.
Usage: python3 reflect_shader.py <path.spv>
Output: JSON with UBO members and texture bindings.
"""
import sys, json, subprocess, re
from pathlib import Path

SPIRV_CROSS = "/usr/local/bin/spirv-cross"

def reflect(spv_path):
    result = {"textures": [], "ubos": []}

    p = subprocess.run([SPIRV_CROSS, "--dump-resources", str(spv_path)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print(f"ERROR: {p.stderr[:300]}", file=sys.stderr)
        return result

    # spirv-cross outputs resource dump to stdout
    text = p.stdout + "\n" + p.stderr  # capture both

    # Parse: ID 005 : u_Texture0 (Set : 1) (Binding : 0)
    for m in re.finditer(r'ID\s+\S+\s+:\s+(\w+)\s+\(Set\s*:\s*(\d+)\)\s*\(Binding\s*:\s*(\d+)\)', text):
        result["textures"].append({
            "name": m.group(1), "set": int(m.group(2)), "binding": int(m.group(3))
        })

    # --reflect outputs to stdout
    p2 = subprocess.run([SPIRV_CROSS, "--reflect", str(spv_path)],
                        capture_output=True, text=True)
    if p2.returncode == 0:
        text2 = p2.stdout + "\n" + p2.stderr
        for m in re.finditer(r'member\s+\d+:\s+(\w+),\s+offset\s+(0x[0-9a-fA-F]+),\s+type\s+(\w+)', text2):
            gtype = m.group(3)
            sizes = {"float":4,"vec2":8,"vec3":12,"vec4":16,"int":4,"ivec2":8,"ivec3":12,"ivec4":16,"uint":4,"uvec2":8,"uvec3":12,"uvec4":16,"mat4":64}
            result["ubos"].append({
                "name": m.group(1),
                "type": gtype,
                "offset": int(m.group(2), 16),
                "size": sizes.get(gtype, 0)
            })

    return result


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 reflect_shader.py <path.spv>")
        sys.exit(1)
    print(json.dumps(reflect(sys.argv[1]), indent=2))
