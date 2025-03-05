#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <filesystem>
#include <algorithm>
#include <locale>
#include "spatialstudio/splv_encoder.h"
#include "spatialstudio/splv_decoder.h"
#include "spatialstudio/splv_nvdb_utils.h"

//-------------------------------------------//

struct BenchmarkResults
{
	uint64_t encodedSize;
	uint64_t rawSizeDense;
	uint64_t rawSizeBrickmap;
	uint64_t rawSizeVoxels;

	float totalTime;
	float totalFrameLoadTime;
	float totalEncodingTime;
	float totalDecodingTime;
};

//-------------------------------------------//

BenchmarkResults run_benchmark_nvdb(uint32_t width, uint32_t height, uint32_t depth, float framerate, 
	                                SPLVencodingParams encodingParams, const std::vector<std::string>& inFiles, 
									SPLVboundingBox bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbaxis, 
									const std::string& outFile);

//-------------------------------------------//

//usage: splv_benchmark -d [width] [height] [depth] -f [framerate] -g [gop size] -b [max brick group size] -m [motion vectors] -i [input direcrory] -o [output file]
int main(int argc, const char** argv)
{
	//parse and validate command line args:
	//---------------
	int32_t width = INT32_MAX;
	int32_t height = INT32_MAX;
	int32_t depth = INT32_MAX;

	float framerate = INFINITY;
	
	int32_t gopSize = 1;
	int32_t maxBrickGroupSize = 256;
	splv_bool_t motionVectors = SPLV_TRUE;

	std::string inDir = "";
	std::string outPath = "";

	for(uint32_t i = 1; i < (uint32_t)argc; i++)
	{
		std::string arg(argv[i]);

		if(arg == "-d") //dimensions
		{
			if(i + 3 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-d\" (need width, height, and depth)" << std::endl;
				return -1;
			}

			try
			{
				width = std::stoi(argv[++i]);
				height = std::stoi(argv[++i]);
				depth = std::stoi(argv[++i]);

				if(width <= 0 || height <= 0 || depth <= 0)
					throw std::invalid_argument("");

				if(width % SPLV_BRICK_SIZE != 0 || height % SPLV_BRICK_SIZE != 0 || depth % SPLV_BRICK_SIZE != 0)
					throw std::invalid_argument("");
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid dimensions" << std::endl;
				return -1;
			}
		}
		else if(arg == "-f") //framerate
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-f\"" << std::endl;
				return -1;
			}

			try
			{
				framerate = std::stof(argv[++i]);

				if(framerate <= 0.0f)
					throw std::invalid_argument("");
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid framerate" << std::endl;
				return -1;
			}
		}
		else if(arg == "-g") //gop size
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-g\"" << std::endl;
				return -1;
			}

			try
			{
				gopSize = std::stoi(argv[++i]);

				if(gopSize <= 0)
					throw std::invalid_argument("");
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid GOP size" << std::endl;
				return -1;
			}
		}
		else if(arg == "-b") //brick group size
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-b\"" << std::endl;
				return -1;
			}

			try
			{
				maxBrickGroupSize = std::stoi(argv[++i]);

				if(maxBrickGroupSize < 0)
					throw std::invalid_argument("");
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid maximum brick group size" << std::endl;
				return -1;
			}
		}
		else if(arg == "-m") //motion vectors
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-m\"" << std::endl;
				return -1;
			}

			std::string option = std::string(argv[++i]);
			if(option == "on")
				motionVectors = SPLV_TRUE;
			else if(option == "off")
				motionVectors = SPLV_FALSE;
			else
			{
				std::cout << "ERROR: invalid maximum motion vectors option" << std::endl;
				return -1;
			}
		}
		else if(arg == "-i") //input directory
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-i\"" << std::endl;
				return -1;
			}

			inDir = std::string(argv[++i]);
		}
		else if(arg == "-o") //output file
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-o\"" << std::endl;
				return -1;
			}

			outPath = std::string(argv[++i]);
		}
		else
		{
			std::cout << "ERROR: unrecognized command line argument \"" << arg << "\"" << std::endl;
			std::cout << "VALID USAGE: splv_encoder -d [width] [height] [depth] -f [framerate] -i [input dir] -g [gop size] -b [max brickgroup size] -m [motion vectors] -o [output file]" << std::endl;
			return -1;
		}
	}

	if(width == INT32_MAX || height == INT32_MAX || depth == INT32_MAX)
	{
		std::cout << "ERROR: no dimensions specified (use \"-d [width] [height] [depth]\")" << std::endl;
		return -1;
	}

	if(framerate == INFINITY)
	{
		std::cout << "ERROR: no framerate specified (use \"-f [framerate]\")" << std::endl;
		return -1;
	}

	if(inDir == "")
	{
		std::cout << "ERROR: no input direcrory specified (use \"-i [input directory]\")" << std::endl;
		return -1;
	}

	if(outPath == "")
	{
		std::cout << "ERROR: no output file specified (use \"-o [output file]\")" << std::endl;
		return -1;
	}

	//get all files in input directory:
	//---------------
	std::vector<std::string> inFiles;
	
	try
	{
		for(const auto& entry : std::filesystem::directory_iterator(inDir)) 
		{
			if(std::filesystem::is_regular_file(entry.status()))
			inFiles.push_back(entry.path().string());
		}
	}
	catch(std::exception e)
	{
		std::cout << "ERROR: error reading input directory" << std::endl;
		return -1;
	}

	std::sort(inFiles.begin(), inFiles.end());

	//run benchmark:
	//---------------
	SPLVencodingParams encodingParams;
	encodingParams.gopSize = gopSize;
	encodingParams.maxBrickGroupSize = maxBrickGroupSize;

	SPLVboundingBox bbox;
	bbox.xMin = 0;
	bbox.yMin = 0;
	bbox.zMin = 0;
	bbox.xMax = width - 1;
	bbox.yMax = height - 1;
	bbox.zMax = depth - 1;

	BenchmarkResults results;
	try
	{
		results = run_benchmark_nvdb(
			width, height, depth, framerate, encodingParams,
			inFiles, bbox, SPLV_AXIS_X, SPLV_AXIS_Y, SPLV_AXIS_Z, 
			outPath
		);
	}
	catch(const std::exception& e)
	{
		std::cout << "ERROR: error running benchmark: " << e.what() << std::endl;
		return -1;
	}

	//print results:
	//---------------
	std::cout.imbue(std::locale("en_US.UTF-8"));
	std::cout << std::fixed << std::setprecision(2);

	std::cout << "BENCHMARK RESULTS:" << std::endl;

	std::cout << "- raw size (dense):    " << results.rawSizeDense    / 1000 << "kB" << std::endl;
	std::cout << "- raw size (brickmap): " << results.rawSizeBrickmap / 1000 << "kB" << std::endl;
	std::cout << "- raw size (voxels):   " << results.rawSizeVoxels   / 1000 << "kB" << std::endl;

	std::cout << "- encoded size:        " << results.encodedSize     / 1000 << "kB" << std::endl;
	std::cout << "\t- " << 100.0f * (float)results.encodedSize / (float)results.rawSizeDense    << "% of raw (dense)"    << std::endl;
	std::cout << "\t- " << 100.0f * (float)results.encodedSize / (float)results.rawSizeBrickmap << "% of raw (brickmap)" << std::endl;
	std::cout << "\t- " << 100.0f * (float)results.encodedSize / (float)results.rawSizeVoxels   << "% of raw (voxels)"   << std::endl;

	std::cout << "- total time: " << results.totalTime / 1000.0f << "s" << std::endl;
	std::cout << "\t- frame load: " << results.totalFrameLoadTime / 1000.0f << "s ("
		<< results.totalFrameLoadTime / inFiles.size() << "ms per frame)" <<  std::endl;
	std::cout << "\t- encoding: "   << results.totalEncodingTime  / 1000.0f << "s ("
		<< results.totalEncodingTime / inFiles.size() << "ms per frame)" <<  std::endl;
	std::cout << "\t- decoding: "   << results.totalDecodingTime  / 1000.0f << "s ("
		<< results.totalDecodingTime / inFiles.size() << "ms per frame)" <<  std::endl;

	return 0;
}

//-------------------------------------------//

BenchmarkResults run_benchmark_nvdb(uint32_t width, uint32_t height, uint32_t depth, float framerate, 
                                    SPLVencodingParams encodingParams, const std::vector<std::string>& inFiles, 
									SPLVboundingBox bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbaxis, 
									const std::string& outFile)
{
	//setup:
	//---------------
	const uint64_t BYTES_PER_VOXEL = 3 * 2 + 3 * 1; //3 xyz position (uint16), 3 rgb color (uint8)

	uint64_t rawSizeDense = 0;
	uint64_t rawSizeBrickmap = 0;
	uint64_t rawSizeVoxels = 0;

	float totalFrameLoadTime = 0.0f;
	float totalEncodingTime = 0.0f;
	float totalDecodingTime = 0.0f;

	//start timing:
	//---------------
	auto startTime = std::chrono::high_resolution_clock::now();

	//encode each frame:
	//---------------
	std::vector<SPLVframe> prevEncodedFrames;

	SPLVencoder encoder;
	SPLVerror encoderError = splv_encoder_create(&encoder, width, height, depth, framerate, encodingParams, outFile.c_str());
	if(encoderError != SPLV_SUCCESS)
	{
		throw std::runtime_error("failed to create encoder with error code " + 
			std::to_string(encoderError) + "(" + splv_get_error_string(encoderError) + ")\n");
	}

	for(uint32_t i = 0; i < inFiles.size(); i++)
	{
		//decode nvdb
		auto frameLoadStartTime = std::chrono::high_resolution_clock::now();

		SPLVframe frame;
		SPLVerror nvdbError = splv_nvdb_load(inFiles[i].c_str(), &frame, &bbox, lrAxis, udAxis, fbaxis);
		if(nvdbError != SPLV_SUCCESS)
		{
			throw std::runtime_error("failed to create nvdb frame with code " +
				std::to_string(nvdbError) + " (" + splv_get_error_string(nvdbError) + ")\n");
		}

		prevEncodedFrames.push_back(frame);

		auto frameLoadEndTime = std::chrono::high_resolution_clock::now();
		auto frameLoadTime = std::chrono::duration_cast<std::chrono::microseconds>(frameLoadEndTime - frameLoadStartTime);
		totalFrameLoadTime += frameLoadTime.count() / 1000.0f;

		//update raw sizes
		rawSizeBrickmap += splv_frame_get_size(&frame);
		rawSizeVoxels += splv_frame_get_num_voxels(&frame) * BYTES_PER_VOXEL;
		rawSizeDense += width * height * depth * 3; //3 bytes per color

		//encode
		auto encodeStartTime = std::chrono::high_resolution_clock::now();

		splv_bool_t canFree;
		SPLVerror encodeError = splv_encoder_encode_frame(&encoder, &frame, &canFree);
		if(encodeError != SPLV_SUCCESS)
		{
			throw std::runtime_error("failed to encode frame with code " +
				std::to_string(encodeError) + " (" + splv_get_error_string(encodeError) + ")\n");
		}

		if(canFree)
		{
			for(uint32_t j = 0; j < (uint32_t)prevEncodedFrames.size(); j++)
				splv_frame_destroy(&prevEncodedFrames[j]);
			prevEncodedFrames.clear();
		}

		auto encodeEndTime = std::chrono::high_resolution_clock::now();
		auto encodeTime = std::chrono::duration_cast<std::chrono::microseconds>(encodeEndTime - encodeStartTime);
		totalEncodingTime += encodeTime.count() / 1000.0f;

		//update progress indicator
		std::cout << "\rencoded " << i + 1 << "/" << inFiles.size();
	}

	SPLVerror finishError = splv_encoder_finish(&encoder);
	if(finishError != SPLV_SUCCESS)
	{
		throw std::runtime_error("failed to finish encoding with code " + 
			std::to_string(finishError) + " (" + splv_get_error_string(finishError) + ")\n");
	}

	for(uint32_t i = 0; i < (uint32_t)prevEncodedFrames.size(); i++)
		splv_frame_destroy(&prevEncodedFrames[i]);
	prevEncodedFrames.clear();

	//get encoded file size:
	//---------------
	std::filesystem::path outPath(outFile);
	uint64_t encodedSize = std::filesystem::file_size(outPath);

	//decode each frame:
	//---------------
	std::vector<SPLVframeIndexed> prevDecodedFrames;

	SPLVdecoder decoder;
	SPLVerror decoderError = splv_decoder_create_from_file(&decoder, outFile.c_str());
	if(decoderError != SPLV_SUCCESS)
	{
		throw std::runtime_error("failed to create decoder with error code " + 
			std::to_string(decoderError) + "(" + splv_get_error_string(decoderError) + ")\n");
	}

	for(uint32_t i = 0; i < inFiles.size(); i++)
	{
		//get dependencies
		uint64_t numDependencies;
		splv_decoder_get_frame_dependencies(&decoder, i, &numDependencies, NULL, 0);

		uint64_t* dependencies = (numDependencies > 0) ? new uint64_t[numDependencies] : NULL;
		splv_decoder_get_frame_dependencies(&decoder, i, &numDependencies, dependencies, 0);

		//free frames that are no longer needed
		for(uint32_t j = 0; j < (uint32_t)prevDecodedFrames.size(); j++)
		{
			bool found = false;
			for(uint32_t k = 0; k < (uint32_t)numDependencies; k++)
			{
				if(dependencies[k] == prevDecodedFrames[j].index)
				{
					found = true;
					break;
				}
			}

			if(!found)
			{
				splv_frame_destroy(prevDecodedFrames[j].frame);
				delete prevDecodedFrames[j].frame;
				prevDecodedFrames.erase(prevDecodedFrames.begin() + j);
				j--;
			}
		}

		//we can safely delete dependencies, since frames can always be decoded in order
		if(numDependencies > 0)
			delete[] dependencies;

		//decode
		SPLVframe* frame = new SPLVframe;

		auto decodeStartTime = std::chrono::high_resolution_clock::now();

		SPLVerror decodeError = splv_decoder_decode_frame(
			&decoder, i, prevDecodedFrames.size(),
			prevDecodedFrames.data(), frame, NULL
		);
		if(decodeError != SPLV_SUCCESS)
		{
			throw std::runtime_error("failed to decode frame with code " +
				std::to_string(decodeError) + " (" + splv_get_error_string(decodeError) + ")\n");
		}

		auto decodeEndTime = std::chrono::high_resolution_clock::now();
		auto decodeTime = std::chrono::duration_cast<std::chrono::microseconds>(decodeEndTime - decodeStartTime);
		totalDecodingTime += decodeTime.count() / 1000.0f;

		//add frame to list
		SPLVframeIndexed indexedFrame;
		indexedFrame.frame = frame;
		indexedFrame.index = i;

		prevDecodedFrames.push_back(indexedFrame);

		//update progress indicator
		std::cout << "\rdecoded " << i + 1 << "/" << inFiles.size();
	}

	splv_decoder_destroy(&decoder);

	//finish timing:
	//---------------
	auto endTime = std::chrono::high_resolution_clock::now();
	auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
	
	//return:
	//---------------
	std::cout << "\r";

	BenchmarkResults results;
	results.encodedSize = encodedSize;
	results.rawSizeBrickmap = rawSizeBrickmap;
	results.rawSizeVoxels = rawSizeVoxels;
	results.rawSizeDense = rawSizeDense;
	results.totalTime = (float)totalTime.count();
	results.totalFrameLoadTime = totalFrameLoadTime;
	results.totalEncodingTime = totalEncodingTime;
	results.totalDecodingTime = totalDecodingTime;

	return results;
}