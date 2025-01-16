from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext
import os
import subprocess
import sys
import platform
import shutil
from pathlib import Path

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
				"CMake must be installed to build the following extensions: " +
				", ".join(e.name for e in self.extensions)
			)

		# build extensions:
		for ext in self.extensions:
			self.build_extension(ext)
			
	def build_extension(self, ext):
		# get build dir
		cmake_build_dir = os.path.abspath(self.build_temp)
		if not os.path.exists(cmake_build_dir):
			os.makedirs(cmake_build_dir)

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
			"-DSPLV_BUILD_PYTHON_BINDINGS=ON"
		]

		# configure CMake
		subprocess.check_call(
			["cmake", ext.sourcedir] + cmake_args,
			cwd=cmake_build_dir
		)

		# build project
		subprocess.check_call(
			["cmake", "--build", ".", "--config", "Release"],
			cwd=cmake_build_dir
		)

		# copy build binary into package dir:
		self.move_output(ext)

	def move_output(self, ext):
		build_temp = os.path.abspath(self.build_temp)
		dest_path = os.path.abspath(self.get_ext_fullpath(ext.name))
		
		standard_filename = self.get_ext_filename(ext.name)
		alt_filename = "splv_encoder_py.pyd" # alternate name from MSVC
		
		source_path = os.path.join(build_temp, standard_filename)
		alt_source_path = os.path.join(build_temp, alt_filename)
		
		if os.path.exists(source_path):
			actual_source = source_path
		elif os.path.exists(alt_source_path):
			actual_source = alt_source_path
		else:
			raise FileNotFoundError(f"neither {source_path} nor {alt_source_path} exists")

		dest_directory = os.path.dirname(dest_path)
		os.makedirs(dest_directory, exist_ok=True)

		self.copy_file(actual_source, dest_path)

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