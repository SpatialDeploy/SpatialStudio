from setuptools import setup, find_packages
import os
import shutil

# source directory (where all binary files are located)
src_bin_dir = os.path.join(os.getcwd(), 'bin')

# package directory (where all binary files will be copied to)
package_bin_dir = os.path.join(os.getcwd(), 'splv_encoder', 'bin')

if not os.path.exists(package_bin_dir):
	os.mkdir(package_bin_dir)

# copy files
for filename in os.listdir(src_bin_dir):
	if filename.endswith('.exe'): # dont copy the command line util
		continue

	src_file = os.path.join(src_bin_dir, filename)
	if os.path.isfile(src_file):
		shutil.copy(src_file, package_bin_dir)

# create package
setup(
	name="splv_encoder",
	version="0.1",
	packages=find_packages(),
	package_data={
		'splv_encoder': ["bin/*"],
	},
	include_package_data=True,
	install_requires=[],
)