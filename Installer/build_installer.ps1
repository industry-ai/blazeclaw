param(
    [string]$candle = "candle.exe",
    [string]$light = "light.exe",
    [string]$VCRedistX64 = "",
    [string]$VCRedistX86 = "",
    [string]$WebView2Installer = "",
    [switch]$SkipBundle = $false
)

# ============================================================
# BlazeClaw Installer Build Script
# ============================================================
# Usage:
#   .\build_installer.ps1                           # Build MSI + Bundle
#   .\build_installer.ps1 -SkipBundle               # Build MSI only
#   .\build_installer.ps1 -VCRedistX64 "path\to\vc_redist.x64.exe"
#   .\build_installer.ps1 -WebView2Installer "path\to\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
#
# Prerequisites:
#   - WiX Toolset v3.11+ installed
#   - BlazeClaw Release build exists in ..\blazeclaw\bin\Release\
# ============================================================

# 查找 WiX 工具（candle.exe / light.exe）
# 参考 healthcare 脚本，列出所有可能的路径依次尝试
function Find-Tool([string]$name) {
    $candidates = @(
        "D:\wix314\$name",
        "D:\wix313\$name",
        "C:\Program Files (x86)\WiX Toolset v3.14\bin\$name",
        "C:\Program Files (x86)\WiX Toolset v3.11\bin\$name",
        "C:\Program Files (x86)\WiX Toolset v4\bin\$name",
        "C:\Program Files\WiX Toolset v4\bin\$name"
    )
    foreach ($c in $candidates) {
        $resolved = $ExecutionContext.InvokeCommand.GetCommand($c, [System.Management.Automation.CommandTypes]::Alias)
        if ($resolved) { $c = $resolved.Source }
        if (Test-Path $c) { return $c }
    }
    return $null
}

# 生成唯一 GUID（基于文件路径 MD5）
function New-UniqueGuid([string]$path) {
    $hash = [System.Security.Cryptography.MD5]::Create().ComputeHash([System.Text.Encoding]::UTF8.GetBytes($path))
    $guid = [Guid]::new([byte[]]($hash[0..15])).ToString().ToUpper()
    return $guid
}

# 将字符串转换为有效的 WiX Id（去除非法字符，限制长度）
function Make-Id([string]$s) {
    $id = ($s -replace "[^A-Za-z0-9_]", "_")
    if ($id.Length -gt 60) { $id = $id.Substring(0,60) }
    return "f_" + $id
}

# ============================================================
# 主流程
# ============================================================

# 所有路径相对于脚本所在目录
$installerDir = $PSScriptRoot

# 向上两级到达仓库根目录（D:\contblazeclaw）
$repoRoot = Split-Path $installerDir -Parent
if ((Split-Path $installerDir -Parent) -eq $repoRoot) {
    # installerDir 就是 Installer，repoRoot 是 contblazeclaw
    $repoRoot = Split-Path $installerDir -Parent
} else {
    # 如果结构不同，使用通用逻辑
    $repoRoot = Split-Path $installerDir -Parent
}

# BlazeClaw 项目根目录
$blazeclawRoot = Join-Path $repoRoot "blazeclaw"

# 构建输出目录（Release/x64）
$buildPath = Join-Path $blazeclawRoot "bin\Release"

# 输出目录（与脚本同目录）
$outputDir = $installerDir

# 查找 WiX 工具
$candlePath = Find-Tool $candle
$lightPath = Find-Tool $light

if (-not $candlePath -or -not $lightPath) {
    Write-Error "WiX tools not found. Please install WiX Toolset v3.11 or higher."
    Write-Output "Expected paths:"
    Write-Output "  - C:\Program Files (x86)\WiX Toolset v3.14\bin\candle.exe"
    Write-Output "  - C:\Program Files (x86)\WiX Toolset v3.11\bin\candle.exe"
    exit 1
}

Write-Output "=========================================="
Write-Output "BlazeClaw Installer Build"
Write-Output "=========================================="
Write-Output "Using candle: $candlePath"
Write-Output "Using light:  $lightPath"
Write-Output "Repository root: $repoRoot"
Write-Output "Build path: $buildPath"
Write-Output ""

# 检查构建目录
if (-not (Test-Path $buildPath)) {
    Write-Error "Build not found: $buildPath"
    Write-Output "Please build BlazeClaw first:"
    Write-Output "  msbuild BlazeClaw.sln /p:Configuration=Release /p:Platform=x64"
    exit 1
}

# 查找主程序 exe
$exeFiles = Get-ChildItem -Path $buildPath -Filter "*.exe" -File -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -ne "WebView2Loader.dll" -and
    $_.DirectoryName -notlike "*WebView2*" -and
    $_.Name -notlike "*_test*" -and
    $_.Name -notlike "*Tests*"
}

$exePath = $null
foreach ($file in $exeFiles) {
    # 匹配 BlazeClaw.exe
    if ($file.Name -eq "BlazeClaw.exe") {
        $exePath = $file.FullName
        break
    }
}

# 如果没找到，尝试第一个 exe
if (-not $exePath -and $exeFiles.Count -gt 0) {
    Write-Warning "BlazeClaw.exe not found, using first exe: $($exeFiles[0].Name)"
    $exePath = $exeFiles[0].FullName
}

if (-not $exePath) {
    Write-Error "EXE not found in $buildPath"
    Write-Output "Files in build directory:"
    Get-ChildItem $buildPath -ErrorAction SilentlyContinue | ForEach-Object { Write-Output "  $($_.Name)" }
    exit 1
}

Write-Output "Found EXE: $exePath"
Write-Output ""

# ============================================================
# 收集所有待打包文件
# ============================================================
$allFiles = @()

# 从构建目录获取文件（排除主 exe、隐藏文件、WebView2 目录）
$buildFiles = Get-ChildItem -Path $buildPath -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -ne "BlazeClaw.exe" -and
    $_.Name -notlike "*BlazeClaw.exe" -and
    -not $_.Attributes.HasFlag([IO.FileAttributes]::Hidden) -and
    $_.DirectoryName -notlike "*WebView2*" -and
    $_.Name -notlike "*.pdb" -and
    $_.Name -notlike "*.log" -and
    $_.Name -notlike "*_test*" -and
    $_.Name -notlike "*Tests*" -and
    $_.Name -notlike "*.ilk" -and
    $_.Name -ne "session_id.txt"
}
$allFiles += $buildFiles

Write-Output "Total files to package: $($allFiles.Count)"

# 按类别统计
$exeCount = ($allFiles | Where-Object { $_.Extension -eq ".exe" }).Count
$dllCount = ($allFiles | Where-Object { $_.Extension -eq ".dll" }).Count
$confCount = ($allFiles | Where-Object { $_.Extension -eq ".conf" }).Count
$webAssetsCount = ($allFiles | Where-Object { $_.FullName -like "*\web\*" }).Count
Write-Output "  - EXE files: $exeCount"
Write-Output "  - DLL files: $dllCount"
Write-Output "  - Config files: $confCount"
Write-Output "  - Web assets: $webAssetsCount"
Write-Output ""

# ============================================================
# 生成 PackageFiles.wxs（文件清单）
# ============================================================

# 构建目录映射
$dirMap = @{}
$dirMap[""] = "INSTALLFOLDER"

foreach ($f in $allFiles) {
    $relPath = $f.FullName.Substring($buildPath.Length).TrimStart('\')
    $dirPart = Split-Path $relPath -Parent
    $dirPart = if ($dirPart) { $dirPart.TrimStart('\') } else { "" }
    if ($dirPart) {
        $parts = $dirPart.Split('\')
        $currentPath = ""
        foreach ($part in $parts) {
            if ($currentPath) { $currentPath = $currentPath + "\" + $part } else { $currentPath = $part }
            if (-not $dirMap.ContainsKey($currentPath)) {
                $safeId = Make-Id $currentPath
                $dirMap[$currentPath] = "dir_$safeId"
            }
        }
    }
}

# 构建目录层级关系
$childrenList = ""
foreach ($key in $dirMap.Keys) {
    if ([string]::IsNullOrEmpty($key)) { continue }
    $parentPath = Split-Path $key -Parent
    $parentKey = if ([string]::IsNullOrEmpty($parentPath)) { "ROOT" } else { $parentPath }
    $childrenList += "$parentKey`t$key`n"
}

function Write-DirectoryTree([string]$parentKey, [int]$indent) {
    $result = ""
    $sp = " " * $indent
    $pattern = "^$([regex]::Escape($parentKey))`t"
    $matches = $childrenList -split "`n" | Where-Object { $_ -match $pattern }
    if ($matches) {
        foreach ($line in ($matches | Sort-Object)) {
            $childPath = $line -replace $pattern, ""
            $childId = $dirMap[$childPath]
            if ([string]::IsNullOrEmpty($childId)) { continue }
            $childName = Split-Path $childPath -Leaf
            $result += "$sp<Directory Id=""$childId"" Name=""$childName"">`n"
            $result += (Write-DirectoryTree $childPath ($indent + 2))
            $result += "$sp</Directory>`n"
        }
    }
    return $result
}

$dirLines = (Write-DirectoryTree "ROOT" 6)

# 生成文件组件
$compLines = @()
$compIndex = 0
foreach ($f in $allFiles) {
    $compIndex++
    $relPath = $f.FullName.Substring($buildPath.Length).TrimStart('\')
    $dirPart = Split-Path $relPath -Parent
    $fileName = $f.Name
    $dirKey = if ($dirPart) { $dirPart.TrimStart('\') } else { "" }
    $dirId = $dirMap[$dirKey]
    if (-not $dirId) {
        Write-Warning "Directory not found for key: '$dirKey' in file: $($f.FullName)"
        $dirId = "INSTALLFOLDER"
    }

    $compId = "cmp_$compIndex"
    $fileId = "fil_$compIndex"
    $guid = New-UniqueGuid $f.FullName

    $compLines += "      <Component Id=""$compId"" Guid=""{$guid}"" Directory=""$dirId"">"
    $compLines += "        <File Id=""$fileId"" Source=""$($f.FullName)"" KeyPath=""yes"" Name=""$fileName"" />"
    $compLines += "      </Component>"
}

# 写入 PackageFiles.wxs
$dirXml = "    <DirectoryRef Id=""INSTALLFOLDER"">`n"
$dirXml += $dirLines
$dirXml += "    </DirectoryRef>"

$wxsContent = @()
$wxsContent += '<?xml version="1.0" encoding="utf-8"?>'
$wxsContent += '<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">'
$wxsContent += "  <Fragment>"
$wxsContent += $dirXml
$wxsContent += "  </Fragment>"
$wxsContent += "  <Fragment>"
$wxsContent += "    <ComponentGroup Id=""PackageComponents"">"
$wxsContent += $compLines
$wxsContent += "    </ComponentGroup>"
$wxsContent += "  </Fragment>"
$wxsContent += "</Wix>"

$wxsPath = Join-Path $installerDir "PackageFiles.wxs"
$wxsContent | Out-File -FilePath $wxsPath -Encoding UTF8
Write-Output "Generated: $wxsPath"
Write-Output ""

# ============================================================
# 第一阶段：编译并链接 MSI
# ============================================================
Write-Output "=========================================="
Write-Output "Building MSI..."
Write-Output "=========================================="

$wixBin = Split-Path $candlePath -Parent
$utilExt = Join-Path $wixBin "WixUtilExtension.dll"
$uiExt = Join-Path $wixBin "WixUIExtension.dll"

# candle 编译
$candleArgs = @()
if (Test-Path $utilExt) { $candleArgs += "-ext"; $candleArgs += $utilExt }
$candleArgs += @(
    "-dSourceExe=$exePath",
    "-o",
    ".\",
    "Product.wxs",
    "PackageFiles.wxs"
)

Write-Output "Running: candle $($candleArgs -join ' ')"
& $candlePath @candleArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "candle failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

# light 链接
$lightArgs = @()
if (Test-Path $uiExt) { $lightArgs += "-ext"; $lightArgs += $uiExt }
if (Test-Path $utilExt) { $lightArgs += "-ext"; $lightArgs += $utilExt }
$lightArgs += "-loc"; $lightArgs += "Product.wxl"
$lightArgs += "-o"; $lightArgs += "BlazeClaw.msi"
$lightArgs += @("Product.wixobj", "PackageFiles.wixobj")

Write-Output "Running: light $($lightArgs -join ' ')"
& $lightPath @lightArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "light failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

$msiPath = Join-Path $installerDir "BlazeClaw.msi"
Write-Output ""
Write-Output "MSI built successfully: $msiPath"
Write-Output ""

# ============================================================
# 第二阶段：编译并链接 Bundle（Setup.exe）
# ============================================================
if ($SkipBundle) {
    Write-Output "Skipping Bundle build (as requested)."
} else {
    Write-Output "=========================================="
    Write-Output "Building Bundle (Setup.exe)..."
    Write-Output "=========================================="

    $balExt = Join-Path $wixBin "WixBalExtension.dll"
    $utilExt = Join-Path $wixBin "WixUtilExtension.dll"

    if (-not (Test-Path $balExt)) {
        Write-Error "WixBalExtension.dll not found. Bundle build requires WiX 3.6+ with Burn."
        Write-Output "Run with -SkipBundle to build MSI only."
        exit 1
    }

    if (-not (Test-Path $msiPath)) {
        Write-Error "MSI not found: $msiPath"
        exit 1
    }

    # candle 编译 Bundle
    $candleBundleArgs = @(
        "-dProductMsi=$msiPath"
    )

    # 添加可选依赖路径（用于嵌入 VC++ Redist 和 WebView2）
    if ($VCRedistX64) {
        $candleBundleArgs += "-dVCRedistX64=$VCRedistX64"
        Write-Output "Including VC++ Redistributable x64: $VCRedistX64"
    } else {
        $candleBundleArgs += "-dVCRedistX64="
    }
    if ($VCRedistX86) {
        $candleBundleArgs += "-dVCRedistX86=$VCRedistX86"
        Write-Output "Including VC++ Redistributable x86: $VCRedistX86"
    } else {
        $candleBundleArgs += "-dVCRedistX86="
    }
    if ($WebView2Installer) {
        $candleBundleArgs += "-dWebView2Installer=$WebView2Installer"
        Write-Output "Including WebView2 Installer: $WebView2Installer"
    } else {
        $candleBundleArgs += "-dWebView2Installer="
    }

    if (Test-Path $balExt) { $candleBundleArgs += "-ext"; $candleBundleArgs += $balExt }
    if (Test-Path $utilExt) { $candleBundleArgs += "-ext"; $candleBundleArgs += $utilExt }

    $candleBundleArgs += "-o"
    $candleBundleArgs += ".\"
    $candleBundleArgs += "Bundle.wxs"

    Write-Output "Running: candle $($candleBundleArgs -join ' ')"
    & $candlePath @candleBundleArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "candle (bundle) failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }

    # light 链接 Bundle
    $lightBundleArgs = @()
    if (Test-Path $balExt) { $lightBundleArgs += "-ext"; $lightBundleArgs += $balExt }
    if (Test-Path $utilExt) { $lightBundleArgs += "-ext"; $lightBundleArgs += $utilExt }
    $lightBundleArgs += "-o"
    $lightBundleArgs += "BlazeClaw-setup.exe"
    $lightBundleArgs += "Bundle.wixobj"

    Write-Output "Running: light $($lightBundleArgs -join ' ')"
    & $lightPath @lightBundleArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "light (bundle) failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }

    $setupPath = Join-Path $installerDir "BlazeClaw-setup.exe"
}

# ============================================================
# 清理中间文件
# ============================================================
Write-Output ""
Write-Output "=========================================="
Write-Output "Cleaning up intermediate files..."
Write-Output "=========================================="

$intermediateFiles = @(
    "Product.wixobj",
    "PackageFiles.wixobj",
    "Bundle.wixobj",
    "BlazeClaw.wixpdb",
    "BlazeClaw-setup.wixpdb"
)

foreach ($file in $intermediateFiles) {
    $path = Join-Path $installerDir $file
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Output "Removed: $file"
    }
}

# ============================================================
# 完成
# ============================================================
Write-Output ""
Write-Output "=========================================="
Write-Output "Build completed!"
Write-Output "=========================================="
Write-Output "MSI:   $msiPath"

if (-not $SkipBundle) {
    Write-Output "Setup: $setupPath"
}

Write-Output ""
Write-Output "Next steps:"
Write-Output "  1. Test the MSI:    msiexec /i BlazeClaw.msi"
Write-Output "  2. Test the Setup: .\BlazeClaw-setup.exe"
Write-Output "  3. Uninstall:       msiexec /x BlazeClaw.msi"
