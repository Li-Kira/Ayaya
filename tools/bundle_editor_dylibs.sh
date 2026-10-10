#!/bin/bash
# 把 AyayaEditor 依赖的动态库打进 .app/Contents/Frameworks，并把 OpenEXR 的绝对 install name 改写为 @rpath。
# 用法: bundle_editor_dylibs.sh <AyayaEditor.app/Contents> <vendor/lib 目录>
set -e

CONTENTS="$1"
VENDOR_LIB="$2"
FW="$CONTENTS/Frameworks"
mkdir -p "$FW"
rm -f "$FW"/*.dylib   # 清掉上次拷贝的只读 dylib，避免 cp 覆盖时 Permission denied

# ---- 1. @rpath 已正确的库，直接拷贝 (ID 已是 @rpath/...) ----
cp /usr/local/lib/libvulkan.1.4.341.dylib "$FW/libvulkan.1.dylib"
cp /usr/local/lib/libMoltenVK.dylib     "$FW/libMoltenVK.dylib"
cp "$VENDOR_LIB/libassimp.dylib"        "$FW/libassimp.dylib"

# ---- 2. OpenEXR 家族: 按逻辑名拷贝 + 改写 ID ----
# (源真实文件 : 逻辑名)
OPENEXR=(
  "/opt/homebrew/opt/openexr/lib/libOpenEXR-3_4.33.3.4.13.dylib:libOpenEXR-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libOpenEXRCore-3_4.33.3.4.13.dylib:libOpenEXRCore-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libIlmThread-3_4.33.3.4.13.dylib:libIlmThread-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libIex-3_4.33.3.4.13.dylib:libIex-3_4.33.dylib"
  "/opt/homebrew/opt/imath/lib/libImath-3_2.30.3.2.2.dylib:libImath-3_2.30.dylib"
  "/opt/homebrew/opt/libdeflate/lib/libdeflate.0.dylib:libdeflate.0.dylib"
  "/opt/homebrew/opt/openjph/lib/libopenjph.0.30.1.dylib:libopenjph.0.30.dylib"
)
for entry in "${OPENEXR[@]}"; do
  src="${entry%%:*}"; name="${entry##*:}"
  cp "$src" "$FW/$name"
  install_name_tool -id "@rpath/$name" "$FW/$name"
done

# ---- 3. 改写绝对 install name → @rpath (可执行文件 + 所有 dylib) ----
# rpath 已由 CMake BUILD_RPATH 注入 @executable_path/../Frameworks，这里无需再 add_rpath
TARGETS=("$CONTENTS/MacOS/AyayaEditor")
for f in "$FW"/*.dylib; do TARGETS+=("$f"); done

MAP=(
  "/opt/homebrew/opt/openexr/lib/libOpenEXR-3_4.33.dylib:@rpath/libOpenEXR-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libOpenEXRCore-3_4.33.dylib:@rpath/libOpenEXRCore-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libIlmThread-3_4.33.dylib:@rpath/libIlmThread-3_4.33.dylib"
  "/opt/homebrew/opt/openexr/lib/libIex-3_4.33.dylib:@rpath/libIex-3_4.33.dylib"
  "/opt/homebrew/opt/imath/lib/libImath-3_2.30.dylib:@rpath/libImath-3_2.30.dylib"
  "/opt/homebrew/opt/libdeflate/lib/libdeflate.0.dylib:@rpath/libdeflate.0.dylib"
  "/opt/homebrew/opt/openjph/lib/libopenjph.0.30.dylib:@rpath/libopenjph.0.30.dylib"
)
for t in "${TARGETS[@]}"; do
  for m in "${MAP[@]}"; do
    abs="${m%%:*}"; rp="${m##*:}"
    install_name_tool -change "$abs" "$rp" "$t" 2>/dev/null || true
  done
done

# ---- 4. 装 MoltenVK ICD (macOS Vulkan loader 自动搜 Resources/vulkan/icd.d) ----
ICD_DIR="$CONTENTS/Resources/vulkan/icd.d"
mkdir -p "$ICD_DIR"
cat > "$ICD_DIR/MoltenVK_icd.json" <<'EOF'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
EOF

# ---- 5. 重新 ad-hoc 签名 (install_name_tool 破坏原签名，Apple Silicon + SIP 下 dyld 拒绝加载) ----
# 先签内部 dylib，再签整个 bundle (密封所有嵌套代码，顺序不能反)
for f in "$FW"/*.dylib; do
  codesign --force --sign - "$f" 2>/dev/null || true
done
codesign --force --sign - "${CONTENTS}/.." 2>/dev/null || true

echo "[bundle] editor dylibs bundled into $FW"
