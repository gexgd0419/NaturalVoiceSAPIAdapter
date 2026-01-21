# 静默安装指南 / Silent Installation Guide

## 概述 / Overview

NaturalVoiceSAPIAdapter 安装程序现在支持静默安装和卸载功能，允许在没有用户界面的情况下自动完成安装过程。

The NaturalVoiceSAPIAdapter installer now supports silent installation and uninstallation, allowing automated installation without user interface.

# 快速参考 / Quick Reference

## Installer.exe 构建指令

提前安装好 Visual Studio Community。

```powershell
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Installer\Installer.vcxproj /p:Configuration=Release /p:Platform=Win32 /v:minimal /t:Clean,Build
```

## 命令速查 / Command Cheat Sheet

### 帮助 / Help
```cmd
Installer.exe -?
```

### 基本安装 / Basic Installation
```cmd
REM GUI 安装 / GUI Installation
Installer.exe

REM 静默安装 / Silent Installation
Installer.exe -silent
Installer.exe -s
```

### 卸载 / Uninstallation
```cmd
REM GUI 卸载 / GUI Uninstallation
Installer.exe -uninstall

REM 静默卸载 / Silent Uninstallation
Installer.exe -silent -uninstall
```

### 常用配置 / Common Configurations

#### 仅讲述人自然语音 / Narrator Voices Only
```cmd
Installer.exe -silent -64bit-only -enable-narrator -no-edge -no-azure
```

#### 仅 Edge 语音 / Edge Voices Only
```cmd
Installer.exe -silent -no-narrator -enable-edge -no-azure
```

#### 企业标准配置 / Enterprise Standard
```cmd
Installer.exe -silent -64bit-only -enable-edge -languages "en-US,zh-CN" -loglevel 1
```

#### 完整配置 / Full Configuration
```cmd
Installer.exe -silent ^
  -enable-narrator ^
  -enable-edge ^
  -enable-azure ^
  -azure-key "your-key" ^
  -azure-region "eastus" ^
  -languages "en-US,zh-CN,ja-JP" ^
  -loglevel 2
```

## 参数速查表 / Parameter Quick Reference

| 参数 / Parameter | 说明 / Description | 示例 / Example |
|------------------|-------------------|----------------|
| `-?`, `-h`, `-help` | 显示帮助 / Show help | `Installer.exe -?` |
| `-silent`, `-s` | 静默模式 / Silent mode | `Installer.exe -s` |
| `-uninstall` | 卸载 / Uninstall | `Installer.exe -uninstall` |
| `-32bit-only` | 仅32位 / 32-bit only | `Installer.exe -s -32bit-only` |
| `-64bit-only` | 仅64位 / 64-bit only | `Installer.exe -s -64bit-only` |
| `-enable-narrator` | 启用讲述人 / Enable Narrator | `Installer.exe -s -enable-narrator` |
| `-no-narrator` | 禁用讲述人 / Disable Narrator | `Installer.exe -s -no-narrator` |
| `-enable-edge` | 启用Edge / Enable Edge | `Installer.exe -s -enable-edge` |
| `-no-edge` | 禁用Edge / Disable Edge | `Installer.exe -s -no-edge` |
| `-enable-azure` | 启用Azure / Enable Azure | `Installer.exe -s -enable-azure` |
| `-no-azure` | 禁用Azure / Disable Azure | `Installer.exe -s -no-azure` |
| `-narrator-path <path>` | 讲述人路径 / Narrator path | `Installer.exe -s -narrator-path "C:\Voices"` |
| `-azure-key <key>` | Azure密钥 / Azure key | `Installer.exe -s -azure-key "abc123"` |
| `-azure-region <region>` | Azure区域 / Azure region | `Installer.exe -s -azure-region "eastus"` |
| `-languages <list>` | 语言列表 / Language list | `Installer.exe -s -languages "en-US,zh-CN"` |
| `-all-languages` | 所有语言 / All languages | `Installer.exe -s -all-languages` |
| `-loglevel <0-6>` | 日志级别 / Log level | `Installer.exe -s -loglevel 2` |
| `-no-phoneme-converters` | 跳过音素转换器 / Skip phoneme converters | `Installer.exe -s -no-phoneme-converters` |

## 退出代码 / Exit Codes

| 代码 / Code | 含义 / Meaning |
|-------------|----------------|
| 0 | 成功 / Success |
| 2 | 文件未找到 / File not found |
| 5 | 访问被拒绝（需要管理员）/ Access denied (need admin) |
| 87 | 参数错误 / Invalid parameter |
| 1223 | 用户取消UAC / User cancelled UAC |

## 日志级别 / Log Levels

| 级别 / Level | 说明 / Description |
|--------------|-------------------|
| 0 | 关闭 / Off |
| 1 | 错误 / Error |
| 2 | 警告 / Warning (默认 / default) |
| 3 | 信息 / Info |
| 4 | 调试 / Debug |
| 5 | 详细 / Verbose |
| 6 | 全部 / All |

## 常见语言代码 / Common Language Codes

| 代码 / Code | 语言 / Language |
|-------------|----------------|
| en-US | 英语（美国）/ English (US) |
| en-GB | 英语（英国）/ English (UK) |
| zh-CN | 中文（简体）/ Chinese (Simplified) |
| zh-TW | 中文（繁体）/ Chinese (Traditional) |
| ja-JP | 日语 / Japanese |
| ko-KR | 韩语 / Korean |
| fr-FR | 法语 / French |
| de-DE | 德语 / German |
| es-ES | 西班牙语 / Spanish |
| it-IT | 意大利语 / Italian |
| pt-BR | 葡萄牙语（巴西）/ Portuguese (Brazil) |
| ru-RU | 俄语 / Russian |

## 故障排除 / Troubleshooting

### 问题：退出代码 5
**解决方案：** 以管理员身份运行
```cmd
REM 右键点击命令提示符 > 以管理员身份运行
REM Right-click Command Prompt > Run as Administrator
```

### 问题：退出代码 2
**解决方案：** 检查文件路径
```cmd
REM 确保 x86 和 x64 文件夹存在
dir x86\NaturalVoiceSAPIAdapter.dll
dir x64\NaturalVoiceSAPIAdapter.dll
```

### 问题：帮助不显示
**解决方案：** 这是正常的，帮助是GUI对话框
```cmd
REM 直接运行，会显示对话框
Installer.exe -?
```

## 验证安装 / Verify Installation

### 检查注册表 / Check Registry
```cmd
REM 32位版本
reg query "HKLM\SOFTWARE\Classes\CLSID\{013ab33b-ad1a-401c-8bee-f6e2b046a94e}\InprocServer32" /reg:32

REM 64位版本
reg query "HKLM\SOFTWARE\Classes\CLSID\{013ab33b-ad1a-401c-8bee-f6e2b046a94e}\InprocServer32" /reg:64
```

### 检查配置 / Check Configuration
```cmd
REM 查看配置
reg query "HKCU\Software\NaturalVoiceSAPIAdapter"
reg query "HKCU\Software\NaturalVoiceSAPIAdapter\Enumerator"
```

## 批量部署脚本模板 / Batch Deployment Script Template

```batch
@echo off
REM 企业部署脚本
REM Enterprise Deployment Script

REM 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Administrator privileges required
    exit /b 1
)

REM 执行静默安装
Installer.exe -silent ^
  -64bit-only ^
  -enable-edge ^
  -languages "en-US,zh-CN" ^
  -loglevel 1

REM 检查结果
if %errorLevel% equ 0 (
    echo Installation successful
) else (
    echo Installation failed: %errorLevel%
    exit /b %errorLevel%
)
```

## PowerShell 部署脚本模板 / PowerShell Deployment Script Template

```powershell
# 检查管理员权限
if (-NOT ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Error "Administrator privileges required"
    exit 1
}

# 执行静默安装
$process = Start-Process -FilePath "Installer.exe" `
    -ArgumentList "-silent", "-64bit-only", "-enable-edge", "-languages", "en-US,zh-CN", "-loglevel", "1" `
    -Wait -PassThru -NoNewWindow

# 检查结果
if ($process.ExitCode -eq 0) {
    Write-Host "Installation successful" -ForegroundColor Green
} else {
    Write-Error "Installation failed: $($process.ExitCode)"
    exit $process.ExitCode
}
```
