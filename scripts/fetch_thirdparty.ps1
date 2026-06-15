# fetch_thirdparty.ps1
# 该脚本用于获取第三方依赖库并复制到 Third-party 目录
# 目前支持的库: imgui (Dear ImGui)

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")
$ThirdPartyDir = Join-Path $ProjectRoot "Third-party"

function Write-Info {
    param([string]$Msg)
    Write-Host "[INFO] $Msg" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Msg)
    Write-Host "[OK]   $Msg" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Msg)
    Write-Host "[WARN] $Msg" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Msg)
    Write-Host "[ERR]  $Msg" -ForegroundColor Red
}

# 确保 Third-party 目录存在
if (-not (Test-Path $ThirdPartyDir)) {
    New-Item -ItemType Directory -Path $ThirdPartyDir -Force | Out-Null
    Write-Info "已创建 Third-party 目录: $ThirdPartyDir"
}

# ============================================================
# 1. Dear ImGui (https://github.com/ocornut/imgui)
# ============================================================
function Get-Imgui {
    $targetDir = Join-Path $ThirdPartyDir "imgui"
    $repoUrl = "https://github.com/ocornut/imgui.git"

    if (Test-Path $targetDir) {
        if ($Force) {
            Write-Warning "imgui 目录已存在，使用 -Force 参数重新克隆..."
            Remove-Item -Recurse -Force $targetDir
        } else {
            Write-Warning "imgui 目录已存在，跳过。(使用 -Force 重新获取)"
            return
        }
    }

    Write-Info "正在克隆 imgui (浅克隆, depth=1)..."
    git clone --depth 1 $repoUrl $targetDir

    if ($LASTEXITCODE -eq 0) {
        Write-Success "imgui 已克隆到: $targetDir"
    } else {
        Write-Error "imgui 克隆失败，请检查网络连接"
        exit 1
    }
}

# ============================================================
# 主流程
# ============================================================
Write-Info "===== 开始获取第三方依赖库 ====="
Get-Imgui
Write-Success "===== 所有第三方库已准备完毕 ====="
