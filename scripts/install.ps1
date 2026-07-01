# One-shot local install for Hyperlite (Windows).
# Usage (from repo root):
#   .\scripts\install.ps1
#   .\scripts\install.ps1 -Python "C:\Path\To\python.exe"
#   .\scripts\install.ps1 -Editable

param(
	[string]$Python = "",
	[switch]$Editable,
	[switch]$SkipCudaPath
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

if ($Python -eq "") {
	$Python = (Get-Command python -ErrorAction SilentlyContinue).Source
	if (-not $Python) {
		throw "Python not found on PATH. Pass -Python explicitly."
	}
}

Write-Host "==> Hyperlite install" -ForegroundColor Cyan
Write-Host "    repo:   $Root"
Write-Host "    python: $Python"

# CUDA runtime DLLs must be discoverable when using the GPU backend.
if (-not $SkipCudaPath) {
	$CudaRoots = @(
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin",
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin",
		"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin"
	)
	foreach ($cudaBin in $CudaRoots) {
		if (Test-Path $cudaBin) {
			$env:PATH = "$cudaBin;$env:PATH"
			Write-Host "    cuda:   $cudaBin" -ForegroundColor DarkGray
			break
		}
	}
}

Write-Host "==> Building + installing package..." -ForegroundColor Cyan
if ($Editable) {
	& $Python -m pip install -e .
} else {
	& $Python -m pip install .
}

Write-Host ""
Write-Host "==> Verifying import..." -ForegroundColor Cyan
$verifyDir = Join-Path $env:TEMP "hyperlite-install-verify"
New-Item -ItemType Directory -Force -Path $verifyDir | Out-Null
Push-Location $verifyDir
$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = ""
& $Python -c "import hyperlite; e = hyperlite.Engine(64, 64, 'cpu', 'test'); print('hyperlite OK:', hyperlite.__file__, 'backend=', e.backend_name())"
$env:PYTHONPATH = $oldPythonPath
Pop-Location

Write-Host ""
Write-Host "Done. Try an example:" -ForegroundColor Green
Write-Host "  python python\examples\pixel_stress.py"
