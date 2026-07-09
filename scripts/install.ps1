# One-shot local install for Hyperlite (Windows).
# Usage (from repo root):
#   .\scripts\install.ps1
#   .\scripts\install.ps1 -Python "C:\Path\To\python.exe"
#   .\scripts\install.ps1 -Editable
#   .\scripts\install.ps1 -SkipPathFix   # don't reorder User PATH

param(
	[string]$Python = "",
	[switch]$Editable,
	[switch]$SkipCudaPath,
	[switch]$SkipPathFix
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

function Resolve-DefaultPython {
	# Prefer the py-launcher default (usually the newest installed CPython).
	if (Get-Command py -ErrorAction SilentlyContinue) {
		$pyExe = & py -c "import sys; print(sys.executable)" 2>$null
		if ($pyExe -and (Test-Path $pyExe)) {
			return $pyExe.Trim()
		}
	}
	$cmd = Get-Command python -ErrorAction SilentlyContinue
	if ($cmd) {
		return $cmd.Source
	}
	throw "Python not found. Install Python 3.10+ or pass -Python explicitly."
}

function Ensure-Python311OnUserPath {
	# WindowsApps python.exe (Store stub) often wins over real installs unless
	# Programs\Python\Python311 is at the front of User PATH.
	$py311 = "$env:LOCALAPPDATA\Programs\Python\Python311"
	$py311Scripts = "$py311\Scripts"
	if (-not (Test-Path "$py311\python.exe")) {
		Write-Host "    path:   Python 3.11 not at $py311 — skipping PATH fix" -ForegroundColor DarkYellow
		return
	}

	$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
	$parts = @($userPath -split ';' | Where-Object { $_ -and $_ -ne $py311 -and $_ -ne $py311Scripts })
	$newPath = ($py311, $py311Scripts) + $parts -join ';'

	if ($userPath -eq $newPath) {
		Write-Host "    path:   User PATH already prioritizes Python 3.11" -ForegroundColor DarkGray
		return
	}

	[Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
	$env:PATH = "$py311;$py311Scripts;" + $env:PATH
	Write-Host "    path:   Prepended Python 3.11 to User PATH (open new terminal for global effect)" -ForegroundColor DarkGray
}

if ($Python -eq "") {
	$Python = Resolve-DefaultPython
}

Write-Host "==> Hyperlite install" -ForegroundColor Cyan
Write-Host "    repo:   $Root"
Write-Host "    python: $Python"

if (-not $SkipPathFix) {
	Ensure-Python311OnUserPath
}

# CUDA runtime DLLs must be discoverable when using the GPU backend.
if (-not $SkipCudaPath) {
	$CudaRoots = @(
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin",
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin",
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin"
	)
	foreach ($cudaBin in $CudaRoots) {
		if (Test-Path $cudaBin) {
			if ($env:PATH -notlike "*$cudaBin*") {
				$env:PATH = "$cudaBin;$env:PATH"
			}
			Write-Host "    cuda:   $cudaBin" -ForegroundColor DarkGray
			break
		}
	}
}

Write-Host "==> Building + installing package..." -ForegroundColor Cyan
if ($Editable) {
	& $Python -m pip install -e .
} else {
	& $Python -m pip install . --force-reinstall
}

Write-Host ""
Write-Host "==> Verifying import (clean dir, no PYTHONPATH)..." -ForegroundColor Cyan
$verifyDir = Join-Path $env:TEMP "hyperlite-install-verify"
New-Item -ItemType Directory -Force -Path $verifyDir | Out-Null
Push-Location $verifyDir
$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = ""
& $Python -c @"
import hyperlite
e = hyperlite.Engine(128, 128, 'gpu', 'verify')
print('hyperlite OK:', hyperlite.__file__)
print('backend:', e.backend_name())
"@
$env:PYTHONPATH = $oldPythonPath
Pop-Location

Write-Host ""
Write-Host "Done. Try an example:" -ForegroundColor Green
Write-Host "  python python\examples\minimal_game.py"
