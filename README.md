# Engine 杀毒引擎

基于 Win32 API 开发的轻量级杀毒软件，提供文件扫描、威胁检测、系统安全管理和辅助工具等功能。

## 功能

### 🔍 扫描检测

- **快速扫描** — 对系统关键目录（System32、启动项、临时目录、下载目录等）进行快速检测，多线程并发扫描，秒级完成
- **自定义扫描** — 用户自行指定单个文件进行扫描
- **MD5 特征码引擎** — 基于黑/白名单 MD5 特征库对文件进行精确匹配
- **启发式引擎** — 扫描文件字节流，发现连续的特征码 `A5 77 B0` 判定为威胁
- **数字签名引擎** — 检测 PE 文件的数字签名状态，有有效签名的文件判定为安全文件
- **脚本分析引擎** — 对 `.bat`/`.cmd`/`.vbs`/`.js`/`.ps1`/`.hta` 等脚本文件进行静态分析，检测恶意/可疑脚本特征，覆盖 PowerShell 编码执行、下载执行、进程注入、持久化，VBScript/JavaScript WSH 危险对象调用，Batch 下载器/持久化命令，以及 Base64/字符串拆分/反射加载等混淆技术
- **信任区管理** — 支持按文件路径、文件夹路径、MD5 值添加信任白名单，优先于黑名单检测

### 🛡️ 系统安全

- **基线检测** — 对标 360 安全卫士的"系统基线检查"功能，覆盖：
  - 账户安全（密码策略、锁定策略、Guest 状态）
  - 系统安全配置（UAC、防火墙、自动更新、屏幕保护、DEP）
  - 注册表安全（自动播放禁用、远程桌面、安全通道、LM 哈希）
  - 网络防护（匿名限制、远程注册表、管理员重命名、默认共享）
  - 安全选项（关机清理页面文件、限制 CD-ROM 访问）

### 🧰 辅助工具

- **软件管理** — 枚举本机所有已安装软件（32/64 位注册表），支持一键卸载
- **文件粉碎机** — 六层策略强制删除被占用的文件（正常删除 → 夺权改权限 → 移动后删除 → RestartManager 关闭句柄 → 计划重启删除 → NtDeleteFile 原生 API）
- **启动项管理** — 仿 Autoruns，枚举和管理所有系统启动项（注册表 Run/RunOnce、启动文件夹、ShellExecuteHooks、AppInit_DLLs、BootExecute、服务、计划任务、Winlogon、BHO、映像劫持、Winsock LSP 等），支持禁用/启用/删除/跳转注册表
- **文件搜索** — 基于 USN Journal 直读 + NtQueryDirectoryFile 的高速文件搜索引擎（前缀树索引），毫秒级搜索
- **进程管理** — 枚举所有进程（含 PID、父 PID、路径、用户、内存、CPU、线程数、是否 64 位/提权），支持枚举线程、强杀进程
- **对象管理器** — 枚举 NT 对象目录，查看对象名称、类型、属性和句柄信息

### 📋 记录与配置

- **扫描历史** — 记录每次扫描的时间、类型、文件统计和威胁列表，支持查看详情和删除记录
- **日志系统** — 按日期自动分割的日志文件，支持 DEBUG / INFO / WARN / ERROR 级别
- **设置中心** — 界面语言切换（中文 / English）、扫描线程数配置（1 / 2 / 4 / 8）

## 环境要求

- Windows 10 / 11
- Visual Studio 2022
- 平台工具集：v143，目标平台：x64

## 构建

用 Visual Studio 打开 `WindowsProject1/WindowsProject1.slnx`，选择配置（Debug / Release），按 `Ctrl+Shift+B` 编译即可。

## 项目结构

```
WindowsProject1/
├── Scanner.h/cpp           # 扫描引擎（MD5 + 启发式 + 签名 + 信任区）
├── MD5Engine.h/cpp         # MD5 兼容转发层
├── MD5Hasher.h/cpp         # MD5 哈希计算
├── HeuristicEngine.h/cpp   # 启发式引擎（A5 77 B0 特征码检测）
├── SignatureEngine.h/cpp   # 数字签名引擎
├── ScriptAnalyzerEngine.h/cpp # 脚本分析引擎（PS/VBS/JS/BAT/HTA）
├── TrustZone.h/cpp         # 信任区管理（SQLite 存储）
├── BaselineEngine.h/cpp    # 基线检测引擎
├── BaselineCommon.h/cpp    # 基线检测公共定义
├── FastSearcher.h/cpp      # 高速文件搜索引擎
├── FileShredder.h/cpp      # 文件粉碎机
├── StartupManager.h/cpp    # 启动项管理
├── ProcessManager.h/cpp    # 进程管理
├── ObjectManager.h/cpp     # NT 对象管理器
├── SoftwareManager.h/cpp   # 软件管理
├── ScanHistory.h/cpp       # 扫描历史
├── Logger.h/cpp            # 日志系统
├── Settings.h/cpp          # 设置中心
├── WindowsProject1.h/cpp   # 主窗口界面
├── FastSearchDialog.h/cpp  # 文件搜索对话框
├── ProcDialog.h/cpp        # 进程管理对话框
├── ObjDialog.h/cpp         # 对象管理对话框
├── BaselineDialog.h/cpp    # 基线检测对话框
├── ShredDialog.h/cpp       # 文件粉碎对话框
├── StartupDialog.h/cpp     # 启动项管理对话框
├── SoftwareDialog.h/cpp    # 软件管理对话框
├── HistoryDialog.h/cpp     # 扫描历史对话框
├── Resource.h              # 资源 ID 定义
└── data/
    ├── black.dat           # MD5 黑名单特征库
    └── white.dat           # MD5 白名单特征库