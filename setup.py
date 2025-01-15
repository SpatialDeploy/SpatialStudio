from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext
import os
import subprocess
import sys
import platform
import shutil

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

		# get cmake args
		cmake_args = [
			f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={cmake_build_dir}",
			f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={cmake_build_dir}",
			"-DCMAKE_BUILD_TYPE=Release",
			"-DSPLV_BUILD_PYTHON_BINDINGS=ON"
		]

		if platform.system() == "Windows":
			python_version = f"{sys.version_info.major}{sys.version_info.minor}"
			python_lib_path = os.path.join(sys.prefix, "libs", f"python{python_version}.lib")
			python_include = os.path.join(sys.prefix, "include")
			
			if os.path.exists(python_lib_path):
				cmake_args += [
					f"-DPYTHON_LIBRARY={python_lib_path}",
					f"-DPYTHON_INCLUDE_DIR={python_include}",
				]
			
			cmake_args += ["-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE"]
			if sys.maxsize > 2**32:
				cmake_args += ["-A", "x64"]
		else:
			cmake_args += ["-DCMAKE_BUILD_TYPE=Release"]

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

		# copy build binaries into package dir:
		package_bin_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'spatialstudio', 'bin')
		os.makedirs(package_bin_dir, exist_ok=True)	

		if os.path.exists(cmake_build_dir):
			for filename in os.listdir(cmake_build_dir):
				src_file = os.path.join(cmake_build_dir, filename)
				if os.path.isfile(src_file):
					shutil.copy2(src_file, package_bin_dir)

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
	package_data={
		"spatialstudio": ["bin/*"],
	},
	classifiers=[
		"Programming Language :: Python :: 3",
		"License :: OSI Approved :: MIT License",
		"Operating System :: OS Independent",
	],
	python_requires=">=3.6",
)