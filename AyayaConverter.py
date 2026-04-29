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
    '.hdr': 2,      # Texture2D
    '.jpg': 2,      # Texture2D
    '.png': 2,      # Texture2D
    '.lua': 6       # LuaScript
}

class AyayaConverterGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Ayaya 场景 UUID 批量升级工具")
        self.root.geometry("650x450")
        self.root.configure(padx=15, pady=15)

        # 1. 顶部：文件夹选择区
        frame_top = tk.Frame(root)
        frame_top.pack(fill=tk.X, pady=(0, 10))

        tk.Label(frame_top, text="项目根目录: ", font=("Arial", 10, "bold")).pack(side=tk.LEFT)
        
        self.path_var = tk.StringVar()
        entry_path = tk.Entry(frame_top, textvariable=self.path_var, state='readonly', width=50)
        entry_path.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(5, 5))
        
        btn_browse = tk.Button(frame_top, text="浏览文件夹...", command=self.browse_folder, bg="#4CAF50", fg="white", relief=tk.FLAT)
        btn_browse.pack(side=tk.RIGHT)

        # 2. 中间：日志输出区
        tk.Label(root, text="处理日志:", font=("Arial", 10)).pack(anchor=tk.W)
        self.log_area = scrolledtext.ScrolledText(root, width=80, height=18, font=("Consolas", 9), bg="#1E1E1E", fg="#D4D4D4")
        self.log_area.pack(fill=tk.BOTH, expand=True, pady=(5, 10))

        # 3. 底部：操作区
        self.btn_start = tk.Button(root, text="🚀 一键扫描并升级所有场景", font=("Arial", 12, "bold"), 
                                   command=self.start_conversion_thread, bg="#008CBA", fg="white", height=2)
        self.btn_start.pack(fill=tk.X)

        self.log("准备就绪。请选择 Ayaya 项目所在的根目录 (包含 Assets 文件夹的目录)。\n")

    def log(self, message):
        """线程安全的日志输出"""
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
        
        # 禁用按钮防止重复点击
        self.btn_start.config(state=tk.DISABLED, text="处理中...")
        
        # 使用子线程运行，防止界面卡死
        threading.Thread(target=self.process_logic, args=(folder,), daemon=True).start()

    def process_logic(self, folder_path):
        registry_file = os.path.join(folder_path, "AssetRegistry.yaml")
        asset_map = {}
        registry_entries = []

        # ==========================================
        # 步骤 1：读取现有的注册表 (智能合并防重复)
        # ==========================================
        if os.path.exists(registry_file):
            try:
                with open(registry_file, 'r', encoding='utf-8') as f:
                    data = yaml.safe_load(f)
                    if data and 'AssetRegistry' in data:
                        for entry in data['AssetRegistry']:
                            asset_map[entry['VirtualPath']] = entry['Handle']
                            registry_entries.append(entry)
                self.log(f"✅ 发现现有账本，已加载 {len(asset_map)} 个历史资产记录。")
            except Exception as e:
                self.log(f"❌ 读取注册表失败: {e}")

        # 辅助函数：获取或生成 UUID
        def get_handle(path):
            if not path or str(path).strip() == "" or path == "None": 
                return 0
            
            # 统一路径分隔符为斜杠
            path = path.replace("\\", "/")
            
            if path in asset_map:
                return asset_map[path]

            handle = random.getrandbits(64)
            asset_map[path] = handle

            ext = os.path.splitext(path)[1].lower()
            asset_type = 4 if "Primitive::" in path else ASSET_TYPES.get(ext, 0)

            registry_entries.append({
                'Handle': handle,
                'Type': asset_type,
                'VirtualPath': path
            })
            self.log(f"[注册新资产] {os.path.basename(path)} -> {handle}")
            return handle

        # ==========================================
        # 步骤 2：递归查找所有 .ayaya 场景文件
        # ==========================================
        ayaya_files = []
        for root_dir, _, files in os.walk(folder_path):
            for f in files:
                if f.endswith('.ayaya'):
                    ayaya_files.append(os.path.join(root_dir, f))

        self.log(f"\n🔍 扫描完毕，找到 {len(ayaya_files)} 个场景文件。开始执行升级替换...")

        # ==========================================
        # 步骤 3：修改场景文件
        # ==========================================
        modified_count = 0
        for file_path in ayaya_files:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    scene_data = yaml.safe_load(f)

                if not scene_data or 'Entities' not in scene_data:
                    continue

                changed = False
                for entity in scene_data['Entities']:
                    # 替换 MeshRendererComponent
                    if 'MeshRendererComponent' in entity:
                        mrc = entity['MeshRendererComponent']
                        if 'ModelPath' in mrc:
                            mrc['ModelHandle'] = get_handle(mrc.pop('ModelPath'))
                            changed = True
                        if 'MaterialPath' in mrc:
                            mrc['MaterialHandle'] = get_handle(mrc.pop('MaterialPath'))
                            changed = True

                    # 替换 EnvironmentComponent
                    if 'EnvironmentComponent' in entity:
                        env = entity['EnvironmentComponent']
                        if 'EquirectangularPath' in env:
                            env['EquirectangularHandle'] = get_handle(env.pop('EquirectangularPath'))
                            changed = True
                        if 'CubemapFaces' in env:
                            env.pop('CubemapFaces')
                            env['CubemapHandle'] = 0
                            changed = True

                # 如果有改动，则覆写原文件
                if changed:
                    with open(file_path, 'w', encoding='utf-8') as f:
                        yaml.dump(scene_data, f, sort_keys=False)
                    self.log(f"  └─ 升级成功: {os.path.relpath(file_path, folder_path)}")
                    modified_count += 1
            except Exception as e:
                self.log(f"❌ 处理文件 {os.path.basename(file_path)} 失败: {e}")

        # ==========================================
        # 步骤 4：保存汇总后的 AssetRegistry.yaml
        # ==========================================
        try:
            with open(registry_file, 'w', encoding='utf-8') as f:
                yaml.dump({'AssetRegistry': registry_entries}, f, sort_keys=False)
            self.log(f"\n💾 账本已保存，当前账本共记录 {len(registry_entries)} 个资产。")
        except Exception as e:
            self.log(f"❌ 保存账本失败: {e}")

        self.log(f"\n🎉 批量升级完成！共处理了 {modified_count} 个场景。请打开 Ayaya 引擎查看效果。")
        
        # 恢复按钮状态
        self.root.after(0, lambda: self.btn_start.config(state=tk.NORMAL, text="🚀 一键扫描并升级所有场景"))
        self.root.after(0, lambda: messagebox.showinfo("升级完成", f"批量升级处理完成！\n成功修改了 {modified_count} 个场景。"))

if __name__ == "__main__":
    root = tk.Tk()
    app = AyayaConverterGUI(root)
    root.mainloop()