# Windows Local LLM Inference - GPU Acceleration Environment Setup Guide

This document provides detailed instructions for installing the complete environment required to run ONNX large language models on Windows, including NVIDIA GPU drivers, CUDA, and cuDNN.

## Table of Contents

- [Prerequisites Check](#prerequisites-check)
- [1. Install NVIDIA GPU Driver](#1-install-nvidia-gpu-driver)
- [2. Install CUDA Toolkit](#2-install-cuda-toolkit)
- [3. Install cuDNN](#3-install-cudnn)
- [4. Install ONNX Runtime GPU Version](#4-install-onnx-runtime-gpu-version)
- [Quick Reference Commands](#quick-reference-commands)
- [External Resources](#external-resources)

---

## Prerequisites Check

### Check GPU Model and Driver Version

1. Right-click Windows Start Menu → Select "Device Manager"
2. Expand "Display adapters" to confirm your NVIDIA GPU model
3. Open NVIDIA Control Panel (right-click desktop → NVIDIA Control Panel)
4. Click "Help" → "System Information" to view current driver version

Or use command line:

```powershell
nvidia-smi
```

Sample output:

```
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 595.79                 Driver Version: 595.79         CUDA Version: 13.2     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                  Driver-Model | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
||=========================================+========================+======================|
|   0  NVIDIA GeForce RTX 5060      WDDM  |   00000000:01:00.0  On |                  N/A |
|  0%   47C    P5             15W /  145W |    1147MiB /   8151MiB |      0%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------+------------------------+----------------------+
```

Note the **CUDA Version** (e.g., 13.2) - this determines the maximum CUDA version you can install.

---

## 1. Install NVIDIA GPU Driver

### Download Latest Stable Driver

**1. Official Auto-Detect Tool (Recommended - one-click detection):**
Link: https://www.nvidia.com/geforce/drivers/
Action: Click "Driver Auto-Detect" → Install NVIDIA Driver Assistant → One-click detection and download latest Game Ready driver (compatible with latest CUDA).

**2. Manual Download (Backup):**
Link: https://www.nvidia.com/Download/index.aspx
Select: Product Type (GeForce) → Product Series (RTX 50 Series) → Product Model (RTX 5060) → OS (Windows 11 64-bit) → Download latest version (≥595.xx).

### Installation Steps

1. Close all programs that use the GPU (browsers, games, AI software)
2. Right-click the downloaded driver installer → Run as administrator
3. Restart your computer after installation completes

### Driver Version Requirements

- It is recommended to install the latest **Game Ready Driver**

---

## 2. Install CUDA Toolkit

### Download CUDA Toolkit

1. Visit [CUDA Download Page](https://developer.nvidia.com/cuda-downloads)
2. Select:
   - OS: Windows → x86_64 → 11/10
   - Installer Type: exe(network) or exe(local)

### Installation Steps

1. Run the downloaded `cuda_*.exe` installer
2. Choose **Express** or **Custom** installation
3. If choosing Custom, ensure the following are checked:
   - ✅ CUDA Development (development components)
   - ✅ CUDA Runtime
   - ✅ CUDA Documentation
   - ✅ CUDA Samples
4. After installation, check the default installation location:

```
C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\
```

### Set Environment Variables

The CUDA installer usually sets environment variables automatically, but verify the following exist:

```powershell
# Check environment variables
$env:CUDA_PATH
$env:PATH
```

Manual setup (if needed):
1. Right-click "This PC" → "Properties" → "Advanced System Settings" → "Environment Variables"
2. In "System Variables", find "Path" → Edit, add the following paths (replace 13.x with your actual version, e.g., 13.2):
   - `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.x\bin`
   - `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.x\lib\x64`
3. Click "OK" to save.

### Verify CUDA Installation

Open a new PowerShell window:

```powershell
nvcc --version
```

Output should be similar to:

```
nvcc: NVIDIA (R) Cuda compiler driver
Copyright (c) 2005-2026 NVIDIA Corporation
Built on Mon_Mar__2_21:54:11_Pacific_Standard_Time_2026
Cuda compilation tools, release 13.2, V13.2.51
Build cuda_13.2.r13.2/compiler.37434383_0
```

---

## 3. Install cuDNN

### Download cuDNN

1. Visit [cuDNN Download Page](https://developer.nvidia.com/cudnn)
2. Click **Download cuDNN**
3. Select cuDNN version that matches your CUDA version
4. Download the **Tarball** package for **Windows x86_64**

### Install cuDNN

1. Extract the downloaded cuDNN archive - you will get three folders: bin, include, lib
2. Copy these three folders to the CUDA installation directory (`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.x`)

Example:
- Copy files from `cuDNN\bin\x64` to `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\bin\x64`
- Copy files from `cuDNN\lib\x64` to `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\lib\x64`
- Copy files from `cuDNN\include` to `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\include`

---

## 4. Install ONNX Runtime GPU Version

### Check ONNX Runtime Version

First, check the ONNX Runtime version used by your project. Check project configuration files:

```powershell
# For C++ projects
Select-String -Path "*.vcxproj" -Pattern "onnxruntime"
Select-String -Path "packages.config" -Pattern "onnxruntime"
```

### C++ Project Installation

For C++ projects:

1. Download the GPU version from [ONNX Runtime GitHub Releases](https://github.com/microsoft/onnxruntime/releases)
2. Extract to project directory or system path
3. Configure project include and library directories

---

## Quick Reference Commands

```powershell
# 1. Check driver
nvidia-smi

# 2. Check CUDA
nvcc --version

# 3. Check cuDNN
Test-Path "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin\cudnn64_*.dll"

# 4. Install ONNX Runtime GPU (Python)
pip install onnxruntime-genai-gpu
```

---

## External Resources

- [NVIDIA Driver Download](https://www.nvidia.com/Download/index.aspx)
- [CUDA Toolkit Download](https://developer.nvidia.com/cuda-downloads)
- [cuDNN Download](https://developer.nvidia.com/cudnn)
