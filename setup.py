"""Local install helper for the Hyperlite native Python extension.

Builds the C++/CUDA engine with CMake, then packages ``hyperlite.pyd`` for
``pip install .`` (local only — not published to PyPI).
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.dist import Distribution


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
RELEASE_DIR = BUILD_DIR / "Release"
PYD_NAME = "hyperlite.pyd"


def _find_pyd() -> Path | None:
	"""Return the built extension path if it exists."""
	for path in (RELEASE_DIR / PYD_NAME, BUILD_DIR / PYD_NAME):
		if path.is_file():
			return path
	for path in BUILD_DIR.rglob(PYD_NAME):
		if path.is_file():
			return path
	return None


def _run_cmake_build() -> None:
	"""Configure and compile the native engine via CMake."""
	if platform.system() != "Windows":
		raise RuntimeError("Hyperlite currently supports Windows only.")

	build_type = os.environ.get("HYPERLITE_BUILD_TYPE", "Release")
	BUILD_DIR.mkdir(parents=True, exist_ok=True)

	subprocess.run(
		[
			"cmake",
			"-S",
			str(ROOT),
			"-B",
			str(BUILD_DIR),
			f"-DCMAKE_BUILD_TYPE={build_type}",
			"-DHYPERLITE_BUILD_PYTHON_BINDINGS=ON",
			"-DHYPERLITE_ENABLE_CUDA=ON",
		],
		check=True,
	)

	build_cmd = ["cmake", "--build", str(BUILD_DIR), "--config", build_type]
	if os.cpu_count():
		build_cmd.extend(["--", "/m"])
	subprocess.run(build_cmd, check=True)


class CMakeBuildExt(build_ext):
	"""Run CMake when needed and stage hyperlite.pyd for wheel/install."""

	def run(self) -> None:
		if _find_pyd() is None:
			print("[hyperlite] Building native extension with CMake...")
			_run_cmake_build()

		pyd = _find_pyd()
		if pyd is None:
			raise RuntimeError(f"Could not find {PYD_NAME} after CMake build under {BUILD_DIR}.")

		dest = Path(self.get_ext_fullpath("hyperlite"))
		dest.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(pyd, dest)
		print(f"[hyperlite] Staged {pyd} -> {dest}")


class BinaryDistribution(Distribution):
	"""Mark wheel as platform-specific (contains a .pyd)."""

	def has_ext_modules(self) -> bool:
		return True


setup(
	ext_modules=[Extension("hyperlite", sources=[])],
	cmdclass={"build_ext": CMakeBuildExt},
	distclass=BinaryDistribution,
	packages=[],
)
