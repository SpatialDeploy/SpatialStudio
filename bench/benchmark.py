import os
import subprocess
import argparse
import git
import glob

# ------------------------------------------- #

def run_benchmarks(datasetDir, benchmarkTool, tempOutFile, framerate, gopSize, maxBrickgroupSize, motionVectors):
	
	for contentDir in glob.glob(os.path.join(datasetDir, '*/')):
		contentName = os.path.basename(os.path.normpath(contentDir))

		for resDir in glob.glob(os.path.join(contentDir, '*/')):
			resName = os.path.basename(os.path.normpath(resDir))
			
			try:
				size = int(resName)
			except ValueError:
				print(f"skipping benchmark {resDir}: invalid resolution")
				continue
			
			benchmarkCmd = [
				benchmarkTool,
				'-d', str(size), str(size), str(size),
				'-f', str(framerate),
				'-g', str(gopSize),
				'-b', str(maxBrickgroupSize),
				'-m', 'on' if motionVectors else 'off',
				'-i', resDir,
				'-o', tempOutFile
			]
			
			print(f"benchmarking {contentName} at {resName}v")
			try:
				subprocess.run(benchmarkCmd, capture_output=False, text=False)
			except Exception as e:
				print(f"error running benchmark: {e}")

			print()

# ------------------------------------------- #

def main():
	
	# constants:
	# ---------------
	DATASET_DIR = 'dataset/'
	TEMP_OUT_FILE = "temp.splv"

	if os.name == 'nt':
		BENCHMARK_TOOL = './splv_benchmark.exe'
	else:
		BENCHMARK_TOOL = './splv_benchmark'

	# parse args:
	# ---------------
	parser = argparse.ArgumentParser(description='Automated Benchmark Runner')
	parser.add_argument('-f', '--framerate', type=int, default=30, 
	                    help='framerate to encode with (default: 30)')
	parser.add_argument('-g', '--gop-size', type=int, default=10, 
	                    help='GOP size to encode with (default: 10)')
	parser.add_argument('-b', '--max-brickgroup-size', type=int, default=512, 
	                    help='max brickgroup size to encode with (default: 512)')
	parser.add_argument('-m', '--use-motion-vectors', type=bool, default=True, 
	                    help='whether or not to encode with motion vectors (default: True)')
	args = parser.parse_args()
	
	# ensure dataset exists:
	# ---------------
	if not os.path.exists(DATASET_DIR):
		print('dataset not found, please clone dataset before running benchmark')
		return

	# run benchmarks:
	# ---------------
	run_benchmarks(
		DATASET_DIR, 
		BENCHMARK_TOOL, 
		TEMP_OUT_FILE,
		args.framerate,
		args.gop_size,
		args.max_brickgroup_size,
		args.use_motion_vectors
	)

# ------------------------------------------- #

if __name__ == '__main__':
	main()