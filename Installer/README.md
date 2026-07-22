# BlazeClaw 安装包制作说明

> 文档版本：v1.0.0  
> 日期：2026-07-22  
> 参照项目：D:\win-healthcare\healthcare\Installer

---

## 一、概述

本文档描述如何使用 WiX Toolset v3 为 BlazeClaw 项目制作 Windows 安装包。

### 1.1 目标产物

| 产物 | 文件名 | 说明 |
|------|--------|------|
| MSI 安装包 | `BlazeClaw.msi` | 标准 Windows Installer 数据库 |
| Burn 引导程序 | `BlazeClaw-setup.exe` | 自解压链式安装器，可检测并安装前置依赖 |

### 1.2 技术栈

| 组件 | 版本 | 说明 |
|------|------|------|
| WiX Toolset | v3.14 | 安装包制作框架 |
| 构建工具 | candle.exe / light.exe | WiX 命令行编译器与链接器 |
| 构建脚本 | PowerShell | build_installer.ps1 |
| 安装框架 | MSI + Burn Bundle | 标准安装包 + 引导程序 |

### 1.3 产品信息

| 字段 | 值 |
|------|------|
| 产品名称 | BlazeClaw |
| 发布者 | 炎图科技 |
| 安装目录 | `[ProgramFilesFolder]\BlazeClaw` |
| 目标平台 | x64 (Windows 10 1809+) |
| 字符集 | Unicode |

---

## 二、目录结构

```
D:\contblazeclaw\
├── Installer/                          # ← 新建安装包项目根目录
│   ├── Product.wxs                     # MSI 产品定义
│   ├── Bundle.wxs                      # Burn Bundle 定义
│   ├── PackageFiles.wxs                # 文件清单（由脚本动态生成）
│   ├── Product.wxl                     # 本地化字符串文件
│   ├── build_installer.ps1             # 构建脚本（核心入口）
│   └── README.md                        # 本文档
│
├── blazeclaw/                          # 主项目目录
│   ├── bin/
│   │   └── Release/                    # ← 打包目标目录
│   │       ├── BlazeClaw.exe           # 主程序
│   │       ├── llama.dll               # LLM 推理引擎
│   │       ├── ggml.dll                # 张量计算库
│   │       ├── ggml-cpu.dll            # CPU 后端
│   │       ├── ggml-base.dll           # 基础后端
│   │       ├── ggml-cuda.dll           # CUDA GPU 后端（可选）
│   │       ├── WebView2Loader.dll      # WebView2 加载器
│   │       ├── onnxruntime.dll         # ONNX 推理引擎
│   │       ├── d3d12.dll               # DirectML 依赖
│   │       ├── blazeclaw.conf          # 配置文件
│   │       ├── web/                    # Web UI 资产
│   │       │   ├── chat/               # 聊天界面
│   │       │   └── agent-chat-vanilla/ # Agent Chat UI
│   │       └── ...
│   ├── BlazeClawMfc/
│   ├── llama.cpp/
│   ├── FunASR/
│   └── ...
└── ...
```

---

## 三、WiX 核心文件说明

### 3.1 Product.wxs（MSI 定义）

**作用**：定义 MSI 安装包的产品信息、安装特性、文件组件、快捷方式、注册表等。

**核心元素**：

| 元素 | 说明 |
|------|------|
| `<Product>` | 产品 Id、名称、版本、发布者、UpgradeCode |
| `<Package>` | 安装程序版本、语言、压缩方式 |
| `<MediaTemplate>` | 嵌入 CAB 压缩包 |
| `<UIRef>` | 使用 WixUI_InstallDir 内置 UI（允许选择安装目录）|
| `<Property>` | 定义安装选项（如是否创建桌面快捷方式）|
| `<Icon>` | 从主程序 exe 提取图标 |
| `<Feature>` | 安装特性（BlazeClaw 主程序 + 全部文件）|
| `<Directory>` | 安装目录结构 |
| `<Component>` | 文件组件（单个文件或文件组）|
| `<RegistryValue>` | 卸载信息写入注册表 |
| `<Shortcut>` | 桌面/开始菜单快捷方式 |
| `<RemoveFolder>` | 卸载时删除文件夹 |

**关键配置**：

```xml
<Product Id="*" 
         Name="BlazeClaw" 
         Language="1033" 
         Version="1.0.0.0"
         Manufacturer="炎图科技"
         UpgradeCode="{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
         Codepage="65001">
  <Package InstallerVersion="500" 
           Compressed="yes" 
           InstallScope="perMachine" />
  <MediaTemplate EmbedCab="yes" />
  <UIRef Id="WixUI_InstallDir" />
  <Property Id="WIXUI_INSTALLDIR" Value="INSTALLFOLDER" />
  <Property Id="CREATE_DESKTOP_SHORTCUT" Value="1" />
  <!-- ... -->
</Product>
```

### 3.2 Bundle.wxs（Burn Bundle 定义）

**作用**：定义引导程序 Setup.exe，链入 MSI 和前置依赖（VC++ Redist、WebView2 等）。

**核心元素**：

| 元素 | 说明 |
|------|------|
| `<Bundle>` | Bundle 名称、版本、发布者、引导程序类型 |
| `<BootstrapperApplicationRef>` | 引导 UI（使用内置 RTF License 页面）|
| `<Chain>` | 安装链（包含 MSI 和前置依赖）|
| `<MsiPackage>` | MSI 安装包 |
| `<ExePackage>` | 前置可执行包（VC++ Redist、WebView2）|

**关键配置**：

```xml
<Bundle Name="BlazeClaw"
        Manufacturer="炎图科技"
        Version="1.0.0.0"
        AppId="XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
        IconSourceFile="$(var.SourceExe)"
        Copyright="Copyright (c) 2026 炎图科技">
  
  <BootstrapperApplicationRef Id="WixStandardBootstrapperApplication.RtfLicense" />
  
  <Chain>
    <!-- WebView2 检测与安装（可选）-->
    <ExePackage Id="WebView2Installer"
                SourceFile="..."
                DownloadUrl="https://go.microsoft.com/fwlink/p/?LinkId=2124703"
                DetectCondition="WebView2DetectCondition" />
    
    <!-- VC++ Redistributable 2022 x64 -->
    <ExePackage Id="VCRedistX64"
                SourceFile="..."
                DownloadUrl="https://aka.ms/vs/17/release/vc_redist.x64.exe"
                DetectCondition="VCRedistX64DetectCondition" />
    
    <!-- 主 MSI -->
    <MsiPackage Id="BlazeClawMsi"
               SourceFile="$(var.ProductMsi)"
               DisplayName="BlazeClaw"
               Vital="yes"
               Compressed="yes"
               DisplayInternalUI="yes" />
  </Chain>
</Bundle>
```

### 3.3 PackageFiles.wxs（文件清单）

**作用**：列出所有需要打包的文件（exe、DLL、配置文件、Web 资产等）。

**生成方式**：由 `build_installer.ps1` 运行时动态扫描构建输出目录生成。

**文件分类**：

| 类别 | 文件示例 | 说明 |
|------|----------|------|
| 主程序 | BlazeClaw.exe | 从 vcxproj 输出目录获取 |
| llama.dll 家族 | llama.dll, ggml.dll, ggml-cpu.dll, ggml-base.dll, ggml-cuda.dll | LLM 推理引擎 |
| 运行时 DLL | WebView2Loader.dll, onnxruntime.dll, d3d12.dll | NuGet 依赖 |
| 配置文件 | blazeclaw.conf | 程序配置 |
| Web 资产 | web/chat/*.html, web/agent-chat-vanilla/* | UI 资源 |

---

## 四、构建流程

### 4.1 构建前置条件

1. **安装 WiX Toolset v3.14**
   ```
   下载地址：https://github.com/wixtoolset/wix3/releases/tag/wix314rtm
   默认安装路径：C:\Program Files (x86)\WiX Toolset v3.14\bin\
   ```

2. **确保 BlazeClaw 项目已构建 Release 版本**
   ```
   构建命令：msbuild BlazeClaw.sln /p:Configuration=Release /p:Platform=x64
   输出目录：blazeclaw/bin/Release/
   ```

### 4.2 构建步骤

执行构建脚本：

```powershell
cd D:\contblazeclaw\Installer
powershell -ExecutionPolicy Bypass -File build_installer.ps1
```

### 4.3 构建流程详解

```
build_installer.ps1
│
├── 1. 查找 WiX 工具
│   └── 依次查找 WiX v3.14 / v3.11 / v4 安装路径
│
├── 2. 扫描构建输出目录
│   ├── blazeclaw/bin/Release/*.exe
│   ├── blazeclaw/bin/Release/*.dll
│   ├── blazeclaw/bin/Release/web/**/*
│   └── blazeclaw/bin/Release/blazeclaw.conf
│
├── 3. 生成 PackageFiles.wxs
│   └── 逐文件生成 <Component> + <File> XML 元素
│
├── 4. 编译 MSI
│   ├── candle Product.wxs PackageFiles.wxs → Product.wixobj + PackageFiles.wixobj
│   └── light Product.wixobj PackageFiles.wixobj → BlazeClaw.msi
│
└── 5. 编译 Bundle
    ├── candle Bundle.wxs → Bundle.wixobj
    └── light Bundle.wixobj → BlazeClaw-setup.exe
```

### 4.4 输出产物

| 产物 | 路径 |
|------|------|
| MSI 安装包 | `Installer/BlazeClaw.msi` |
| Burn 引导程序 | `Installer/BlazeClaw-setup.exe` |
| 调试符号 | `Installer/BlazeClaw.wixpdb` |

---

## 五、打包内容清单

### 5.1 必须包含的文件

| 文件 | 来源 | 说明 |
|------|------|------|
| BlazeClaw.exe | bin/Release | 主程序 |
| llama.dll | bin/Release | LLM 推理引擎 |
| ggml.dll | bin/Release | 张量计算库 |
| ggml-cpu.dll | bin/Release | CPU 后端 |
| ggml-base.dll | bin/Release | 基础后端 |
| ggml-cuda.dll | bin/Release | CUDA GPU 后端 |
| blazeclaw.conf | bin/Release | 配置文件 |
| web/chat/**/* | bin/Release/web/chat | 聊天界面资源 |
| web/agent-chat-vanilla/**/* | bin/Release/web/agent-chat-vanilla | Agent Chat 资源 |

### 5.2 条件包含的文件

| 文件 | 条件 | 说明 |
|------|------|------|
| WebView2Loader.dll | NuGet 包 | WebView2 运行时加载器 |
| onnxruntime.dll | NuGet 包 | ONNX 推理引擎 |
| d3d12.dll | 系统 DLL | DirectML GPU 加速依赖 |
| opencl.dll | 系统 DLL | OpenCL GPU 加速（可选）|

### 5.3 不包含的文件

| 文件/目录 | 原因 |
|-----------|------|
| 模型文件 (models/*.gguf, models/*.onnx) | 模型较大，由用户自行下载 |
| bin/Debug/* | Debug 构建产物 |
| *.pdb | 调试符号（Release 构建可选择包含）|
| sms_login_test.log 等日志文件 | 测试文件，不应分发 |
| vcruntime140d.dll, ucrtbased.dll | Debug 运行时库，不应出现在 Release 安装包 |

---

## 六、注册表与快捷方式

### 6.1 卸载注册表

安装包会在卸载时写入以下注册表信息（供「程序和功能」显示）：

```
HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{ProductId}
├── DisplayName     = "BlazeClaw"
├── UninstallString = "msiexec /x {ProductId}"
├── DisplayIcon    = "[INSTALLFOLDER]BlazeClaw.exe"
├── NoModify       = 1
├── NoRepair       = 1
├── Publisher      = "炎图科技"
├── DisplayVersion = "1.0.0.0"
└── EstimatedSize  = XXXXX (KB)
```

### 6.2 桌面快捷方式

- 快捷方式名称：**BlazeClaw**
- 目标路径：`[INSTALLFOLDER]BlazeClaw.exe`
- 工作目录：`[INSTALLFOLDER]`
- 图标：从主程序 exe 中提取
- 条件属性：`CREATE_DESKTOP_SHORTCUT=1`（默认为 1，可通过命令行参数覆盖）

### 6.3 开始菜单快捷方式

当前设计为可选，默认不创建。如需启用，可在 `Product.wxs` 中添加 `StartMenuShortcuts` 组件。

---

## 七、前置依赖检测

### 7.1 Visual C++ Redistributable 2022

**检测条件**：`VCRedistX64DetectCondition`

```xml
<ExePackage Id="VCRedistX64"
            SourceFile="..."
            DownloadUrl="https://aka.ms/vs/17/release/vc_redist.x64.exe"
            DetectCondition="VCRedistX64DetectCondition" />
```

WiX Burn 内置了对 VC++ Redist 的检测，但也可以指定本地 SourceFile 让其直接安装。

### 7.2 WebView2 Runtime

**检测条件**：`WebView2DetectCondition`

```xml
<ExePackage Id="WebView2Installer"
            SourceFile="..."
            DownloadUrl="https://go.microsoft.com/fwlink/p/?LinkId=2124703"
            DetectCondition="WebView2DetectCondition" />
```

WebView2 是 Edge Chromium 的 Web 组件，Windows 10 1809+ 通常预装，但需要检测版本。

---

## 八、升级与卸载

### 8.1 升级策略

- **UpgradeCode**：固定 GUID，更换 Product Id 即可触发升级检测
- **Version**：每次发布递增（格式：主.次.修订.构建，如 1.0.0.0）
- **Minor Upgrades**：设置 `<MajorUpgrade>` 自动处理旧版本卸载

```xml
<MajorUpgrade DowngradeErrorMessage="A newer version of [ProductName] is already installed." />
```

### 8.2 多版本共存

如果需要支持多版本共存：
- 设置 `<Product>` 的 `Id` 为固定 GUID（而非 `*`）
- 移除 `<MajorUpgrade>` 元素

---

## 九、构建脚本详解（build_installer.ps1）

### 9.1 核心功能

| 功能 | 说明 |
|------|------|
| WiX 工具发现 | 自动搜索 WiX v3/v4 安装路径 |
| 文件扫描 | 扫描 Release 构建输出，收集所有待打包文件 |
| PackageFiles.wxs 生成 | 动态生成文件清单 XML |
| candle 编译 | 将 .wxs 编译为 .wixobj |
| light 链接 | 将 .wixobj 链接为 .msi 和 .exe |
| 清理中间文件 | 删除 .wixobj 等编译产物 |

### 9.2 关键变量

| 变量 | 说明 | 示例值 |
|------|------|--------|
| `$WiXRoot` | WiX 安装根目录 | `C:\Program Files (x86)\WiX Toolset v3.14` |
| `$SourceDir` | BlazeClaw Release 构建输出目录 | `D:\contblazeclaw\blazeclaw\bin\Release` |
| `$InstallerOutput` | 安装包输出目录 | `D:\contblazeclaw\Installer` |
| `$ProductName` | 产品名称 | `BlazeClaw` |
| `$Manufacturer` | 发布者 | `炎图科技` |
| `$Version` | 产品版本 | `1.0.0.0` |
| `$UpgradeCode` | 升级代码 | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` |

### 9.3 文件扫描规则

```powershell
# 包含规则
*.exe          # 主程序
*.dll          # 所有 DLL（llama、ggml、WebView2、ONNX 等）
*.conf         # 配置文件
web/**/*.*     # Web UI 资产
*.ico          # 图标文件

# 排除规则
*d.dll         # Debug DLL（vcruntime140d.dll 等）
*.pdb          # 调试符号
*.log          # 日志文件
*_test*        # 测试文件
```

---

## 十、常见问题

### Q1: 安装包体积太大怎么办？

**方案**：
1. 排除 Debug DLL（vcruntime140d.dll、ucrtbased.dll）
2. 排除 .pdb 调试符号
3. 排除非必要的 Web 资产
4. 模型文件不打包（用户自行下载）
5. 使用 `<MediaTemplate EmbedCab="yes" />` 压缩

### Q2: 如何支持静默安装？

**方案**：
- MSI：`msiexec /i BlazeClaw.msi /qn`
- Bundle：`BlazeClaw-setup.exe /quiet /norestart`

### Q3: 如何在安装时传递参数？

**方案**：
- MSI：`msiexec /i BlazeClaw.msi PROPERTY=VALUE`
- Bundle：`BlazeClaw-setup.exe /passive CREATE_DESKTOP_SHORTCUT=0`

### Q4: WiX v3 和 v4 如何选择？

| 方面 | WiX v3 | WiX v4 |
|------|--------|--------|
| 稳定性 | 非常稳定 | 相对较新 |
| 工具链 | candle.exe / light.exe | wix.exe |
| Burn Bundle | 原生支持 | 需要额外扩展 |
| .NET 要求 | 无 | 需要 .NET 6+ |
| 文档 | 丰富 | 仍在完善 |
| 推荐场景 | 生产环境 | 新项目探索 |

**建议**：BlazeClaw 使用 WiX v3（与 healthcare 保持一致）。

### Q5: 如何处理 CUDA GPU 支持？

**方案**：
- `ggml-cuda.dll` 仅在检测到 NVIDIA GPU 时安装
- 可通过 `<Condition>` 或自定义 `CustomAction` 检测
- 简化处理：始终包含，CUDA 会自动回退到 CPU

---

## 十一、文件清单

创建 Installer 目录时，需要创建以下文件：

```
Installer/
├── Product.wxs          # MSI 产品定义（需手动编写模板）
├── Bundle.wxs          # Burn Bundle 定义（需手动编写模板）
├── PackageFiles.wxs    # 文件清单（由 build_installer.ps1 动态生成，无需手动创建）
├── Product.wxl         # 本地化字符串（可选，当前为空占位）
├── build_installer.ps1 # 构建脚本（核心，需手动编写）
└── README.md            # 本文档
```

---

## 十二、实施步骤

### Step 1: 安装 WiX Toolset v3.14

下载并安装：https://github.com/wixtoolset/wix3/releases/tag/wix314rtm

### Step 2: 创建 Installer 目录

```powershell
mkdir D:\contblazeclaw\Installer
```

### Step 3: 编写 Product.wxs

参照 healthcare/Installer/Product.wxs 模板，修改以下内容：
- 产品名称 → BlazeClaw
- 发布者 → 炎图科技
- UpgradeCode → 新生成的 GUID
- 安装目录 → BlazeClaw

### Step 4: 编写 Bundle.wxs

参照 healthcare/Installer/Bundle.wxs 模板，修改以下内容：
- Bundle 名称 → BlazeClaw
- 发布者 → 炎图科技
- MsiPackage SourceFile 路径

### Step 5: 编写 build_installer.ps1

核心功能：
1. 查找 WiX 工具路径
2. 扫描 Release 构建输出
3. 生成 PackageFiles.wxs
4. 执行 candle / light 编译链接
5. 输出 BlazeClaw.msi 和 BlazeClaw-setup.exe

### Step 6: 测试构建

```powershell
cd D:\contblazeclaw\Installer
powershell -ExecutionPolicy Bypass -File build_installer.ps1
```

### Step 7: 测试安装

1. 双击 BlazeClaw-setup.exe
2. 按照向导完成安装
3. 验证桌面快捷方式和开始菜单
4. 运行 BlazeClaw.exe
5. 通过「程序和功能」卸载

---

## 附录 A：参考资源

| 资源 | 链接 |
|------|------|
| WiX v3 官方文档 | https://wixtoolset.org/docs/ |
| WiX v3 下载 | https://github.com/wixtoolset/wix3/releases |
| Burn Bundle 文档 | https://wixtoolset.org/docs/bundle/ |
| healthcare Installer 源码 | D:\win-healthcare\healthcare\Installer |

## 附录 B：版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-07-22 | 初始版本 |

---
