import os
import yaml
import random
import threading
import tkinter as tk
from tkinter import filedialog, scrolledtext, messagebox

# ==========================================
# Ayaya 资产类型定义
# ==========================================
ASSET_TYPES = {
    '.mat': 5,      # Material
    '.obj': 4,      # Model
    '.fbx': 4,      # Model
    '.gltf': 4,     # Model
    '.cube': 3,     # TextureCube
    '.hdr': 2,      # Texture2D
    '.jpg': 2,      # Texture2D
    '.png': 2,      # Texture2D
    '.lua': 6       # LuaScript
}

class AyayaConverterGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Ayaya 全局资产建档与 UUID 升级工具 (材质扁平结构修复版)")
        self.root.geometry("750x550")
        self.root.configure(padx=15, pady=15)

        frame_top = tk.Frame(root)
        frame_top.pack(fill=tk.X, pady=(0, 10))

        tk.Label(frame_top, text="项目根目录: ", font=("Arial", 10, "bold")).pack(side=tk.LEFT)
        
        self.path_var = tk.StringVar()
        entry_path = tk.Entry(frame_top, textvariable=self.path_var, state='readonly', width=50)
        entry_path.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(5, 5))
        
        btn_browse = tk.Button(frame_top, text="浏览文件夹...", command=self.browse_folder, bg="#4CAF50", fg="white", relief=tk.FLAT)
        btn_browse.pack(side=tk.RIGHT)

        tk.Label(root, text="处理日志:", font=("Arial", 10)).pack(anchor=tk.W)
        self.log_area = scrolledtext.ScrolledText(root, width=80, height=22, font=("Consolas", 9), bg="#1E1E1E", fg="#D4D4D4")
        self.log_area.pack(fill=tk.BOTH, expand=True, pady=(5, 10))

        self.btn_start = tk.Button(root, text="🚀 全局扫描建档 并 升级所有文件", font=("Arial", 12, "bold"), 
                                   command=self.start_conversion_thread, bg="#008CBA", fg="white", height=2)
        self.btn_start.pack(fill=tk.X)

        self.log("准备就绪。\n请选择 Ayaya 项目所在的根目录 (必须包含 Assets 文件夹)。\n")

    def log(self, message):
        self.root.after(0, self._append_log, message)

    def _append_log(self, message):
        self.log_area.insert(tk.END, message + "\n")
        self.log_area.see(tk.END)

    def browse_folder(self):
        folder_selected = filedialog.askdirectory(title="选择 Ayaya 项目根目录")
        if folder_selected:
            self.path_var.set(folder_selected)
            self.log(f"已选中目录: {folder_selected}")

    def start_conversion_thread(self):
        folder = self.path_var.get()
        if not folder or not os.path.isdir(folder):
            messagebox.showwarning("提示", "请先选择一个有效的项目文件夹！")
            return
        
        self.btn_start.config(state=tk.DISABLED, text="处理中...")
        threading.Thread(target=self.process_logic, args=(folder,), daemon=True).start()

    def process_logic(self, folder_path):
        assets_dir = os.path.join(folder_path, "Assets")
        if not os.path.exists(assets_dir):
            assets_dir = os.path.join(folder_path, "assets") 
        
        if not os.path.exists(assets_dir):
            self.log("❌ 错误: 在所选目录下没有找到 Assets 文件夹！")
            self.root.after(0, lambda: self.btn_start.config(state=tk.NORMAL, text="🚀 全局扫描建档 并 升级所有文件"))
            return

        registry_file = os.path.join(assets_dir, "AssetRegistry.yaml")
        registry_entries = []
        norm_to_handle = {}

        def normalize_path(p):
            if not p: return ""
            p = p.replace("\\", "/").lower()
            p = p.replace("project://", "")
            p = p.replace("engine://", "")
            p = p.strip("/")
            if p.startswith("assets/"):
                p = p[7:].strip("/")
            return p

        # ==========================================
        # 阶段 1：读取账本
        # ==========================================
        old_wrong_registry = os.path.join(folder_path, "AssetRegistry.yaml")
        for reg_file in [registry_file, old_wrong_registry]:
            if os.path.exists(reg_file):
                try:
                    with open(reg_file, 'r', encoding='utf-8') as f:
                        data = yaml.safe_load(f)
                        if data and 'AssetRegistry' in data:
                            for entry in data['AssetRegistry']:
                                fixed_path = entry['VirtualPath'].replace("\\", "/")
                                if fixed_path.startswith("project://Assets/"):
                                    fixed_path = fixed_path.replace("project://Assets/", "project://", 1)
                                elif fixed_path.startswith("project://assets/"):
                                    fixed_path = fixed_path.replace("project://assets/", "project://", 1)
                                elif not fixed_path.startswith("project://") and not fixed_path.startswith("engine://") and not "Primitive::" in fixed_path:
                                    fixed_path = f"project://{fixed_path}"
                                
                                entry['VirtualPath'] = fixed_path
                                registry_entries.append(entry)
                                norm_to_handle[normalize_path(fixed_path)] = entry['Handle']
                    self.log(f"✅ 已加载账本 {os.path.basename(reg_file)}")
                except Exception as e:
                    self.log(f"❌ 读取账本失败: {e}")

        if os.path.exists(old_wrong_registry) and old_wrong_registry != registry_file:
            try: os.remove(old_wrong_registry)
            except: pass

        # ==========================================
        # 阶段 2：全局物理扫描
        # ==========================================
        ayaya_files = []
        mat_files = []
        new_assets_count = 0

        self.log("\n🔍 开始全局扫描物理文件...")
        for root_dir, _, files in os.walk(folder_path):
            for f in files:
                ext = os.path.splitext(f)[1].lower()
                abs_path = os.path.join(root_dir, f)
                
                if ext == '.ayaya':
                    ayaya_files.append(abs_path)
                elif ext == '.mat':
                    mat_files.append(abs_path)
                
                if ext in ASSET_TYPES:
                    if abs_path.lower().startswith(assets_dir.lower()):
                        rel_path = os.path.relpath(abs_path, assets_dir).replace("\\", "/")
                    else:
                        rel_path = os.path.relpath(abs_path, folder_path).replace("\\", "/")
                        if rel_path.lower().startswith("assets/"):
                            rel_path = rel_path[7:].strip("/")
                            
                    virt_path = f"project://{rel_path}"
                    norm_p = normalize_path(virt_path)

                    if norm_p not in norm_to_handle:
                        handle = random.getrandbits(64)
                        norm_to_handle[norm_p] = handle
                        registry_entries.append({
                            'Handle': handle,
                            'Type': ASSET_TYPES[ext],
                            'VirtualPath': virt_path
                        })
                        self.log(f"  [新资产建档] {virt_path} -> {handle}")
                        new_assets_count += 1

        def get_handle(old_path):
            if not old_path or str(old_path).strip() == "" or old_path == "None": return 0
            if "Primitive::" in old_path:
                norm_p = normalize_path(old_path)
                if norm_p not in norm_to_handle:
                    handle = random.getrandbits(64)
                    norm_to_handle[norm_p] = handle
                    registry_entries.append({'Handle': handle, 'Type': 4, 'VirtualPath': old_path})
                return norm_to_handle[norm_p]

            norm_old = normalize_path(old_path)
            for key_path, handle in norm_to_handle.items():
                if key_path.endswith(norm_old) or norm_old.endswith(key_path):
                    return handle
            return 0

        # ==========================================
        # 阶段 3 & 4：升级场景与材质
        # ==========================================
        self.log("\n🛠️ 开始升级文件内容...")
        modified_files = 0
        
        for file_path in ayaya_files + mat_files:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    file_data = yaml.safe_load(f)

                if not file_data: continue
                changed = False

                # 场景文件处理
                if 'Entities' in file_data:
                    for entity in file_data['Entities']:
                        if 'MeshRendererComponent' in entity:
                            mrc = entity['MeshRendererComponent']
                            if 'ModelPath' in mrc:
                                mrc['ModelHandle'] = get_handle(mrc.pop('ModelPath'))
                                changed = True
                            if 'MaterialPath' in mrc:
                                mrc['MaterialHandle'] = get_handle(mrc.pop('MaterialPath'))
                                changed = True
                        
                        if 'EnvironmentComponent' in entity:
                            env = entity['EnvironmentComponent']
                            if 'EquirectangularPath' in env:
                                env['EquirectangularHandle'] = get_handle(env.pop('EquirectangularPath'))
                                changed = True
                                
                            if 'CubemapFaces' in env:
                                faces = env.pop('CubemapFaces')
                                valid_faces = [f for f in faces if f and str(f).strip() != ""]
                                
                                if len(valid_faces) == 6:
                                    cube_filename = f"LegacyCubemap_{random.randint(1000, 9999)}.cube"
                                    cube_filepath = os.path.join(os.path.dirname(file_path), cube_filename)
                                    
                                    cube_data = {
                                        'Cubemap': {
                                            'Right': faces[0], 'Left': faces[1],
                                            'Top': faces[2], 'Bottom': faces[3],
                                            'Front': faces[4], 'Back': faces[5]
                                        }
                                    }
                                    with open(cube_filepath, 'w', encoding='utf-8') as cf:
                                        yaml.dump(cube_data, cf, sort_keys=False)
                                    
                                    rel_cube = os.path.relpath(cube_filepath, assets_dir).replace("\\", "/")
                                    virt_cube = f"project://{rel_cube}"
                                    cube_handle = random.getrandbits(64)
                                    norm_to_handle[normalize_path(virt_cube)] = cube_handle
                                    registry_entries.append({
                                        'Handle': cube_handle,
                                        'Type': 3,
                                        'VirtualPath': virt_cube
                                    })
                                    
                                    env['CubemapHandle'] = cube_handle
                                    self.log(f"  [自动迁移] 成功提取旧版天空盒为 -> {cube_filename}")
                                else:
                                    env['CubemapHandle'] = 0
                                changed = True

                        if 'SpriteRendererComponent' in entity:
                            src = entity['SpriteRendererComponent']
                            if 'TexturePath' in src:
                                src['TextureHandle'] = get_handle(src.pop('TexturePath'))
                                changed = True
                        if 'LuaScriptComponent' in entity:
                            lsc = entity['LuaScriptComponent']
                            if 'ScriptPath' in lsc:
                                lsc['ScriptHandle'] = get_handle(lsc.pop('ScriptPath'))
                                changed = True

                # ==========================================
                # 【核心修复】：直接检查最外层的 'Properties'
                # 无论它是 .mat 还是别的文件，只要根节点有 Properties 且里面有 TexturePath 就升级！
                # ==========================================
                if 'Properties' in file_data:
                    for prop in file_data['Properties']:
                        if 'TexturePath' in prop:
                            # 弹出旧的 TexturePath，如果有空字符串 ""，get_handle 会安全地返回 0
                            prop['TextureHandle'] = get_handle(prop.pop('TexturePath'))
                            changed = True

                if changed:
                    with open(file_path, 'w', encoding='utf-8') as f:
                        yaml.dump(file_data, f, sort_keys=False)
                    self.log(f"  [文件已修复] {os.path.basename(file_path)}")
                    modified_files += 1
            except Exception as e:
                self.log(f"❌ 处理文件 {os.path.basename(file_path)} 失败: {e}")

        try:
            with open(registry_file, 'w', encoding='utf-8') as f:
                yaml.dump({'AssetRegistry': registry_entries}, f, sort_keys=False)
            self.log(f"\n💾 账本已保存至: Assets/AssetRegistry.yaml！当前总资产数: {len(registry_entries)} 个。")
        except Exception as e:
            self.log(f"❌ 保存账本失败: {e}")

        self.log(f"\n🎉 彻底完工！\n修复场景和材质共计: {modified_files} 个")
        
        self.root.after(0, lambda: self.btn_start.config(state=tk.NORMAL, text="🚀 全局扫描建档 并 升级所有文件"))
        self.root.after(0, lambda: messagebox.showinfo("升级完成", f"项目的 UUID 升级已圆满完成！\n成功修复了 {modified_files} 个文件。"))

if __name__ == "__main__":
    root = tk.Tk()
    app = AyayaConverterGUI(root)
    root.mainloop()