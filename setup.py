from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext
import os
import subprocess
import glob
from pathlib import Path
import sys

# ------------------------------------------ #


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


# ------------------------------------------ #


class CMakeBuild(build_ext):
    def run(self):
        # ensure cmake is installed:
        try:
            subprocess.check_output(["cmake", "--version"])
        except OSError:
            raise RuntimeError(
                "CMake must be installed to build the following extensions: "
                + ", ".join(e.name for e in self.extensions)
            )

        # build extensions:
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        # get build dir
        cmake_build_dir = os.path.abspath(self.build_temp)
        os.makedirs(cmake_build_dir, exist_ok=True)
        python_executable = sys.executable

        # get cmake args (force all output files to be in root build directory)
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={cmake_build_dir}",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={cmake_build_dir}",
            f"-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY={cmake_build_dir}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE={cmake_build_dir}",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE={cmake_build_dir}",
            f"-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE={cmake_build_dir}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG={cmake_build_dir}",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG={cmake_build_dir}",
            f"-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG={cmake_build_dir}",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DPython3_EXECUTABLE={python_executable}",
            "-DSPLV_BUILD_PYTHON_BINDINGS=ON",
        ]

        # configure CMake
        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=cmake_build_dir
        )

        # build project
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", "Release"], cwd=cmake_build_dir
        )

        # copy build binary into package dir:
        self.move_output(ext)

    def move_output(self, ext):
        build_temp = os.path.abspath(self.build_temp)
        dest_path = os.path.abspath(self.get_ext_fullpath(ext.name))
        dest_directory = os.path.dirname(dest_path)

        if os.name == "nt":  # windows
            pattern = "splv_encoder_py*.pyd"
        else:  # unix
            pattern = "splv_encoder_py*.so"

        matching_files = glob.glob(os.path.join(build_temp, pattern))

        if not matching_files:
            raise FileNotFoundError(
                f"no files matching {pattern} found in {build_temp}"
            )

        os.makedirs(dest_directory, exist_ok=True)

        for source_path in matching_files:
            dest_file = os.path.join(dest_directory, os.path.basename(source_path))
            self.copy_file(source_path, dest_file)


# ------------------------------------------ #

# read README for long description
with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="spatialstudio",
    version="0.1",
    author="Daniel Elwell",
    author_email="frozeinyt@gmail.com",
    description="Utilities for creating spatials",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/SpatialDeploy/SpatialStudio",
    packages=find_packages(),
    ext_modules=[CMakeExtension("splv_encoder_py")],
    cmdclass={"build_ext": CMakeBuild},
    install_requires=[],
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.6",
)
