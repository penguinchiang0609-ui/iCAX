"""Offline TPM license issuer. Never generate production keys on a client PC."""
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from issuance import Issuer, read_request
from production import ProductionIssuer


class TestWindow:
    def __init__(self, root):
        self.root = root
        self.issuer = None
        self.request = None
        root.title("TubeDesigner 离线签发工具 — 测试版")
        root.geometry("1000x650")
        frame = ttk.Frame(root, padding=16)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="仅供联调：设备硬件可信证明尚未接入，不得签发正式授权。",
                  foreground="#b42318").pack(anchor="w")
        toolbar = ttk.Frame(frame)
        toolbar.pack(fill="x", pady=12)
        for text, action in [("新建测试签发库", self.initialize), ("打开签发库", self.open),
                             ("导入申请", self.import_request), ("导出选中证书", self.export)]:
            ttk.Button(toolbar, text=text, command=lambda f=action: self.guard(f)).pack(side="left", padx=4)
        self.status = tk.StringVar(value="尚未打开签发库")
        ttk.Label(frame, textvariable=self.status, wraplength=950).pack(anchor="w", pady=6)
        form = ttk.Frame(frame)
        form.pack(fill="x")
        self.fields = {}
        for row, (label, default) in enumerate([("客户名称", ""), ("授权类型", "永久"),
                                               ("最低主版本", "0"), ("最高主版本", "0"), ("试用天数", "30")]):
            ttk.Label(form, text=label).grid(row=row, column=0, sticky="w", pady=4)
            variable = tk.StringVar(value=default)
            self.fields[label] = variable
            if label == "授权类型":
                entry = ttk.Combobox(form, textvariable=variable, values=["永久", "试用"], state="readonly")
            else:
                entry = ttk.Entry(form, textvariable=variable, width=40)
            entry.grid(row=row, column=1, sticky="w", padx=12)
        features = ttk.Frame(frame)
        features.pack(fill="x", pady=12)
        self.features = []
        for title in ["设计", "拆单", "STEP 导出", "下料排样"]:
            variable = tk.BooleanVar(value=True)
            self.features.append(variable)
            ttk.Checkbutton(features, text=title, variable=variable).pack(side="left", padx=6)
        ttk.Button(frame, text="审核并签发测试证书", command=lambda: self.guard(self.issue)).pack(anchor="w")
        self.table = ttk.Treeview(frame, columns=("customer", "request", "device"), show="tree headings", height=10)
        for col, title in [("#0", "授权编号"), ("customer", "客户"), ("request", "申请编号"), ("device", "设备公钥摘要")]:
            self.table.heading(col, text=title)
            self.table.column(col, width=220)
        self.table.pack(fill="both", expand=True, pady=12)

    def guard(self, action):
        try:
            action()
        except Exception as error:
            messagebox.showerror("操作未完成", str(error) or type(error).__name__)

    def password(self):
        value = simpledialog.askstring("签发私钥", "请输入私钥口令（不会保存）：", show="*")
        if value is None:
            raise ValueError("已取消")
        return value.encode("utf-8")

    def initialize(self):
        parent = filedialog.askdirectory(title="选择父目录，将在其中新建签发库")
        if not parent:
            return
        password = self.password()
        if password != self.password():
            raise ValueError("两次口令不一致")
        issuer = Issuer(Path(parent) / "TubeDesigner-Test-Issuer")
        issuer.initialize_test(password)
        self.issuer = issuer
        self.refresh()

    def open(self):
        folder = filedialog.askdirectory(title="打开测试签发库")
        if folder:
            self.issuer = Issuer(Path(folder))
            self.refresh()

    def refresh(self):
        records = self.issuer.records()
        for item in self.table.get_children():
            self.table.delete(item)
        for license_id, customer, request, device, _ in records:
            self.table.insert("", "end", iid=license_id, text=license_id, values=(customer, request, device))
        self.status.set("签发库：" + str(self.issuer.directory))

    def import_request(self):
        path = filedialog.askopenfilename(title="导入测试授权申请", filetypes=[("授权申请", "*.tdreq"), ("所有文件", "*.*")])
        if path:
            body, _, digest = read_request(Path(path))
            self.request = Path(path)
            self.status.set("申请：" + body["request_id"] + "；摘要：" + digest + "；仅验证密钥持有，不证明 TPM 来源")

    def issue(self):
        if self.issuer is None or self.request is None:
            raise ValueError("请先打开签发库并导入申请")
        kind = 1 if self.fields["授权类型"].get() == "永久" else 2
        if not messagebox.askyesno("确认签发", "确认签发测试证书？这不是可分发的正式授权。"):
            return
        identifier = self.issuer.issue(self.request, self.password(), customer=self.fields["客户名称"].get(),
            kind=kind, features=sum(1 << i for i, var in enumerate(self.features) if var.get()),
            min_major=int(self.fields["最低主版本"].get()), max_major=int(self.fields["最高主版本"].get()),
            days=int(self.fields["试用天数"].get()))
        self.refresh()
        self.table.selection_set(identifier)
        self.export()

    def export(self):
        selected = self.table.selection()
        if self.issuer is None or not selected:
            raise ValueError("请选择一条签发记录")
        path = filedialog.asksaveasfilename(title="导出证书（不覆盖已有文件）", initialfile=selected[0] + ".tdlic",
                                          defaultextension=".tdlic")
        if path:
            self.issuer.export(selected[0], Path(path))
            messagebox.showinfo("已导出", "证书已导出；可从台账再次导出同一证书。")


class Window(TestWindow):
    """Production UI; hardware certificate validation cannot be bypassed."""
    def __init__(self, root):
        super().__init__(root)
        root.title("TubeDesigner 离线授权签发")
        frame = root.winfo_children()[0]
        frame.winfo_children()[0].configure(text="正式签发：先验证 TPM 厂商证书链，再生成绑定硬件的激活文件。请在隔离电脑运行。")
        toolbar = frame.winfo_children()[1]
        toolbar.winfo_children()[0].configure(text="新建签发库")
        toolbar.winfo_children()[3].configure(text="导出选中授权")
        extra = ttk.Frame(frame)
        extra.pack(fill="x", before=frame.winfo_children()[2])
        for title, action in [("导入厂商根证书", lambda: self.import_ca(True)),
                              ("导入中间证书", lambda: self.import_ca(False)), ("导出构建公钥", self.export_trust),
                              ("备份签发库", self.backup)]:
            ttk.Button(extra, text=title, command=lambda f=action: self.guard(f)).pack(side="left", padx=4)
        for widget in frame.winfo_children():
            if isinstance(widget, ttk.Button):
                widget.configure(text="审核并签发授权")

    def initialize(self):
        if not messagebox.askyesno("隔离签发电脑", "确认当前是你控制的隔离签发电脑？正式私钥不能在客户电脑或在线开发电脑生成。"):
            return
        parent = filedialog.askdirectory(title="选择父目录，新建 TubeDesigner-Issuer")
        if not parent:
            return
        password = self.password()
        if password != self.password():
            raise ValueError("两次口令不一致")
        issuer = ProductionIssuer(Path(parent) / "TubeDesigner-Issuer")
        issuer.initialize(password)
        self.issuer = issuer
        self.refresh()

    def open(self):
        folder = filedialog.askdirectory(title="打开正式签发库")
        if folder:
            issuer = ProductionIssuer(folder)
            issuer.configuration()
            self.issuer = issuer
            self.request = None
            self.refresh()

    def import_request(self):
        if self.issuer is None:
            raise ValueError("请先打开签发库")
        path = filedialog.askopenfilename(title="导入授权申请", filetypes=[("授权申请", "*.tdreq")])
        if path:
            self.request = None
            request, fingerprint = self.issuer.inspect(path)
            self.request = Path(path)
            existing = [row for row in self.issuer.records() if row[3] == __import__('hashlib').sha256(request.ek_area).hexdigest()]
            self.status.set(f"硬件证书链验证通过；申请 {request.digest}；同设备已有 {len(existing)} 条签发记录；EK 证书 {fingerprint}")

    def issue(self):
        if self.issuer is None or self.request is None:
            raise ValueError("请先打开签发库并验证申请")
        if not messagebox.askyesno("确认签发", "确认客户、版本及功能范围无误？签发后离线永久授权无法远程撤销。"):
            return
        identifier = self.issuer.issue(self.request, self.password(), customer=self.fields["客户名称"].get(),
            kind=1 if self.fields["授权类型"].get() == "永久" else 2,
            features=sum(1 << i for i, var in enumerate(self.features) if var.get()),
            min_major=int(self.fields["最低主版本"].get()), max_major=int(self.fields["最高主版本"].get()),
            days=int(self.fields["试用天数"].get()))
        self.refresh()
        self.table.selection_set(identifier)
        self.export()

    def export(self):
        selected = self.table.selection()
        if self.issuer is None or not selected:
            raise ValueError("请选择签发记录")
        path = filedialog.asksaveasfilename(title="导出硬件绑定激活文件", initialfile=selected[0] + ".tdact", defaultextension=".tdact")
        if path:
            self.issuer.export(selected[0], path)
            messagebox.showinfo("已导出", "把 .tdact 文件交给客户导入激活。不要交付私钥或签发库。")

    def import_ca(self, trusted):
        if self.issuer is None:
            raise ValueError("请先打开签发库")
        path = filedialog.askopenfilename(title="选择独立取得的 TPM 厂商 CA 证书", filetypes=[("DER 证书", "*.cer")])
        if path:
            from production import read_bounded
            import hashlib
            fingerprint = hashlib.sha256(read_bounded(path, 16384)).hexdigest()
            if trusted and not messagebox.askyesno("增加信任根", "SHA-256：" + fingerprint + "\n你是否已从厂商官方资料独立核对该根证书及指纹？不能信任客户随申请提供的根证书。"):
                return
            self.issuer.add_certificate(path, trusted_root=trusted)
            messagebox.showinfo("已导入", "证书仅加入此离线签发库，不修改 Windows 系统证书库。")

    def export_trust(self):
        if self.issuer is None:
            raise ValueError("请先打开签发库")
        path = filedialog.asksaveasfilename(title="导出客户端构建信任头文件", initialfile="IssuerTrust.generated.h")
        if path:
            self.issuer.export_build_trust(path)
            messagebox.showinfo("已导出", "只包含公钥和签发者编号；将此文件交给客户端构建，不包含私钥。")

    def backup(self):
        if self.issuer is None:
            raise ValueError("请先打开签发库")
        parent = filedialog.askdirectory(title="选择安全备份位置（建议离线加密介质）")
        if parent:
            import datetime
            output = Path(parent) / ("TubeDesigner-Issuer-Backup-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
            self.issuer.backup(output)
            messagebox.showinfo("备份完成", "已备份加密私钥、公钥、台账及厂商证书。请保管好介质和私钥口令。\n" + str(output))


if __name__ == "__main__":
    import sys
    root = tk.Tk()
    if "--self-test" in sys.argv:
        root.withdraw()
    Window(root)
    if "--self-test" in sys.argv:
        ProductionIssuer.native_verifier()
        root.update_idletasks()
        root.destroy()
    else:
        root.mainloop()
