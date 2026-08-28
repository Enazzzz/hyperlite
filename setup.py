"""Local install helper for the Hyperlite native Python extension.

Builds the C++/CUDA engine with CMake, then packages ``hyperlite.pyd`` (Windows)
or ``hyperlite*.so`` (Linux / macOS) for ``pip install .`` (local only — not published to PyPI).
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.dist import Distribution


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
RELEASE_DIR = BUILD_DIR / "Release"


def _extension_names() -> list[str]:
	"""Candidate filenames for the built native module on this platform."""
	if platform.system() == "Windows":
		return ["hyperlite.pyd"]
	suffix = sysconfig.get_config_var("EXT_SUFFIX") or ".so"
	return [f"hyperlite{suffix}", "hyperlite.so"]


def _find_extension() -> Path | None:
	"""Return the built extension path if it exists."""
	names = _extension_names()
	for name in names:
		for path in (RELEASE_DIR / name, BUILD_DIR / name):
			if path.is_file():
				return path
	for name in names:
		for path in BUILD_DIR.rglob(name):
			if path.is_file():
				return path
	# Fallback: any hyperlite shared object produced by CMake.
	for pattern in ("hyperlite*.so", "hyperlite*.pyd", "hyperlite*.dylib"):
		matches = sorted(BUILD_DIR.rglob(pattern))
		if matches:
			return matches[0]
	return None


def _cmake_executable() -> str:
	"""Return a cmake binary, preferring PATH then the PyPI `cmake` package."""
	found = shutil.which("cmake")
	if found:
		return found
	try:
		import cmake as cmake_pkg

		candidate = Path(cmake_pkg.CMAKE_BIN_DIR) / "cmake"
		if candidate.is_file():
			return str(candidate)
	except (ImportError, AttributeError):
		pass
	raise RuntimeError(
		"cmake not found. On macOS: `brew install cmake` (or `pip install cmake`). "
		"On Linux: `sudo apt-get install cmake`."
	)


def _require_python_headers() -> None:
	"""Fail early when this interpreter has no Python.h (common on macOS /usr/bin/python3)."""
	include = sysconfig.get_path("include")
	header = Path(include) / "Python.h" if include else None
	if header is None or not header.is_file():
		exe = sys.executable
		raise RuntimeError(
			f"Python.h not found for {exe} (looked in {include!r}). "
			"On macOS do not use /usr/bin/python3 — install Homebrew python "
			"(`brew install python`) or python.org, then recreate the venv."
		)


def _host_osx_arch() -> str | None:
	"""Single-arch macOS slice so python.org universal2 does not dual-compile."""
	if platform.system() != "Darwin":
		return None
	machine = platform.machine()
	if machine in ("arm64", "aarch64"):
		return "arm64"
	if machine == "x86_64":
		return "x86_64"
	return None


def _require_apple_toolchain() -> None:
	"""Fail early on macOS when Xcode CLT / clang++ is missing."""
	if platform.system() != "Darwin":
		return
	select = subprocess.run(["xcode-select", "-p"], capture_output=True, text=True)
	if select.returncode != 0 or not (select.stdout or "").strip():
		raise RuntimeError(
			"Xcode Command Line Tools are not selected. Run: xcode-select --install "
			"(on macOS Tahoe / 26, install the matching CLT 26 package)."
		)
	if shutil.which("clang++") is None:
		raise RuntimeError("clang++ not on PATH. Run: xcode-select --install")


def _run_cmake_build() -> None:
	"""Configure and compile the native engine via CMake."""
	_require_apple_toolchain()
	_require_python_headers()
	build_type = os.environ.get("HYPERLITE_BUILD_TYPE", "Release")
	BUILD_DIR.mkdir(parents=True, exist_ok=True)

	# Always bind the extension to the Python running pip — reusing a CMake cache
	# built for a different interpreter produces a module linked to the wrong runtime.
	python_exe = Path(sys.executable).resolve()
	# macOS has no CUDA on Apple Silicon; skip the toolkit probe unless forced.
	enable_cuda = "ON"
	if platform.system() == "Darwin" and os.environ.get("HYPERLITE_ENABLE_CUDA", "0") != "1":
		enable_cuda = "OFF"
	cmake = _cmake_executable()
	cmake_cmd = [
		cmake,
		"-S",
		str(ROOT),
		"-B",
		str(BUILD_DIR),
		f"-DCMAKE_BUILD_TYPE={build_type}",
		"-DHYPERLITE_BUILD_PYTHON_BINDINGS=ON",
		f"-DHYPERLITE_ENABLE_CUDA={enable_cuda}",
		f"-DPython3_EXECUTABLE={python_exe}",
	]
	osx_arch = _host_osx_arch()
	if osx_arch:
		cmake_cmd.append(f"-DCMAKE_OSX_ARCHITECTURES={osx_arch}")
	# Prefer g++ on Linux so OpenMP (-fopenmp) links cleanly.
	if platform.system() == "Linux" and shutil.which("g++"):
		cmake_cmd.append("-DCMAKE_CXX_COMPILER=g++")
	elif platform.system() == "Darwin" and shutil.which("clang++"):
		cmake_cmd.append("-DCMAKE_CXX_COMPILER=clang++")

	subprocess.run(cmake_cmd, check=True)

	build_cmd = [cmake, "--build", str(BUILD_DIR), "--config", build_type, "-j", str(os.cpu_count() or 2)]
	subprocess.run(build_cmd, check=True)


class CMakeBuildExt(build_ext):
	"""Run CMake when needed and stage the native module for wheel/install."""

	def run(self) -> None:
		print(f"[hyperlite] Building for Python {sys.executable}")
		found = _find_extension()
		if found is not None:
			# Force relink against the current Python — stale modules may be cached.
			for pattern in ("hyperlite*.so", "hyperlite*.pyd", "hyperlite*.dylib"):
				for path in BUILD_DIR.rglob(pattern):
					path.unlink(missing_ok=True)
		print("[hyperlite] Building native extension with CMake...")
		_run_cmake_build()

		ext_path = _find_extension()
		if ext_path is None:
			raise RuntimeError(
				f"Could not find hyperlite native module after CMake build under {BUILD_DIR}. "
				"On macOS this usually means Python.h was missing — use Homebrew or python.org "
				"Python, not /usr/bin/python3."
			)

		dest = Path(self.get_ext_fullpath("hyperlite"))
		dest.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(ext_path, dest)
		print(f"[hyperlite] Staged {ext_path} -> {dest}")


class BinaryDistribution(Distribution):
	"""Mark wheel as platform-specific (contains a native extension)."""

	def has_ext_modules(self) -> bool:
		return True


setup(
	ext_modules=[Extension("hyperlite", sources=[])],
	cmdclass={"build_ext": CMakeBuildExt},
	distclass=BinaryDistribution,
	packages=[],
)
