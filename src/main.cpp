#include <iostream>

#define NANOVDB_USE_BLOSC
#include <nanovdb/util/IO.h>

#include "splv_encoder.hpp"

#define MAX_ARGS_LEN 256

//-------------------------------------------//

Axis parse_axis(std::string s)
{
	if(s == "x")
		return Axis::X;
	else if(s == "y")
		return Axis::Y;
	else if(s == "z")
		return Axis::Z;
	else
		throw std::invalid_argument("");
}

//-------------------------------------------//

//usage: splv_encoder -d [xSize] [ySize] [zSize] -a [left/right axis] [up/down axis] [front/back axis] -f [framerate] -o [output file]
int main(int argc, const char** argv)
{
	//parse and validate command line args:
	//---------------
	int32_t xSize = INT32_MAX;
	int32_t ySize = INT32_MAX;
	int32_t zSize = INT32_MAX;

	Axis lrAxis = (Axis)UINT32_MAX;
	Axis udAxis = (Axis)UINT32_MAX;
	Axis fbAxis = (Axis)UINT32_MAX;

	float framerate = INFINITY;
	
	std::string outPath = "";

	for(uint32_t i = 1; i < (uint32_t)argc; i++)
	{
		std::string arg(argv[i]);

		if(arg == "-d") //dimensions
		{
			if(i + 3 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-d\" (need xSize, ySize, and zSize)" << std::endl;
				return -1;
			}

			try
			{
				xSize = std::stoi(argv[++i]);
				ySize = std::stoi(argv[++i]);
				zSize = std::stoi(argv[++i]);

				if(xSize <= 0 || ySize <= 0 || zSize <= 0)
					throw std::invalid_argument("");
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid dimensions" << std::endl;
				return -1;
			}
		}
		else if(arg == "-a")
		{
			if(i + 3 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-a\" (need left/right axis, up/down axis, and front/back axis)" << std::endl;
				return -1;
			}

			try
			{
				lrAxis = parse_axis(std::string(argv[++i]));
				udAxis = parse_axis(std::string(argv[++i]));
				fbAxis = parse_axis(std::string(argv[++i]));
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid axes" << std::endl;
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
			std::cout << "VALID USAGE: splv_encoder -d [xSize] [ySize] [zSize] -f [framerate] -o [output file]" << std::endl;
			return -1;
		}
	}

	if(xSize == INT32_MAX || ySize == INT32_MAX || zSize == INT32_MAX)
	{
		std::cout << "ERROR: no dimensions specified (use \"-d [xSize] [ySize] [zSize]\")" << std::endl;
		return -1;
	}

	if(lrAxis == (Axis)UINT32_MAX || udAxis == (Axis)UINT32_MAX || fbAxis == (Axis)UINT32_MAX)
	{
		std::cout << "ERROR: no axes specified (use \"-a [left/right axis] [up/down axis] [front/back axis]\")" << std::endl;
		return -1;
	}

	if(framerate == INFINITY)
	{
		std::cout << "ERROR: no framerate specified (use \"-f [framerate]\")" << std::endl;
		return -1;
	}

	if(outPath == "")
	{
		std::cout << "ERROR: no output file specified (use \"-o [output file]\")" << std::endl;
		return -1;
	}

	//create outfile and encoder:
	//---------------
	SPLVEncoder* encoder = nullptr;
	std::ofstream* outFile;

	try
	{
		outFile = new std::ofstream(outPath, std::ofstream::binary);
		if(!outFile->is_open())
			throw std::runtime_error("failed to open output file for writing: " + outPath);

		encoder = new SPLVEncoder((uint32_t)xSize, (uint32_t)ySize, (uint32_t)zSize, lrAxis, udAxis, fbAxis, framerate, *outFile);
	}
	catch(std::exception e)
	{
		std::cout << "ERROR: " << e.what() << std::endl;
		return -1;
	}

	//print welcome message:
	//---------------
	std::cout << "===================================" << std::endl;
	std::cout << "            SPLV Encoder           " << std::endl;
	std::cout << "===================================" << std::endl;
	std::cout << "- \"a [path/to/nvdb]\"" << std::endl;
	std::cout << "- \"b [minX] [minY] [minZ] [maxX] [maxY] [maxZ]\" to set the bounding box of all subsequent frames" << std::endl;
	std::cout << "- \"s [on/off]\" to enable/disable scale to fit on all subsequent frames" << std::endl;
	std::cout << "- \"f\" to finish encoding and exit program" << std::endl;
	std::cout << "- \"q\" to exit program without finishing encoding" << std::endl;
	std::cout << std::endl;

	//check for commands in loop:
	//---------------
	int32_t minX = 0;
	int32_t minY = 0; 
	int32_t minZ = 0;
	int32_t maxX = xSize - 1;
	int32_t maxY = ySize - 1;
	int32_t maxZ = zSize - 1;

	while(true)
	{
		std::cout << "> ";

		std::string input;
		std::getline(std::cin, input);
		std::istringstream stream(input);

		std::string command;
		if(!(stream >> command))
			continue;

		if(command == "a")
		{
			std::string path;
			if(!(stream >> path))
			{
				std::cout << "ERROR: no NVDB file specified" << std::endl;
				continue;
			}

			try
			{
				auto file = nanovdb::io::readGrid(path);
				auto* grid = file.grid<nanovdb::Vec3f>();
				if(!grid)
					throw std::runtime_error("NVDB file specified did not contain a Vec3f grid");

				encoder->add_nvdb_frame(grid, nanovdb::CoordBBox(nanovdb::Coord(minX, minY, minZ), nanovdb::Coord(maxX, maxY, maxZ)));
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: " << e.what() << std::endl;
			}
		}
		else if(command == "b")
		{
			uint32_t newMinX, newMinY, newMinZ;
			uint32_t newMaxX, newMaxY, newMaxZ;
			if (!(stream >> newMinX >> newMinY >> newMinZ >> newMaxX >> newMaxY >> newMaxZ)) 
			{
				std::cout << "ERROR: not enough coordinates specified for bounding box" << std::endl;
				continue;
			}

			minX = newMinX;
			minY = newMinY;
			minZ = newMinZ;
			maxX = newMaxX;
			maxY = newMaxY;
			maxZ = newMaxZ;
		}
		else if(command == "s")
		{
			/*std::string option;
			if(!(stream >> option))
			{
				std::cout << "ERROR: no parameter given to \"s\"" << std::endl;
				continue;
			}

			if(option == "on")
				scaleToFit = true;
			else if(option == "off")
				scaleToFit = false;
			else
			{
				std::cout << "ERROR: invalud parameter given to \"s\" (expects \"on\" or \"off\")" << std::endl;
				continue;	
			}*/
		}
		else if(command == "f")
		{
			try
			{
				encoder->finish();
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: " << e.what() << std::endl;
				continue;
			}

			break;
		}
		else if(command == "q")
			break;
		else
			std::cout << "ERROR: unrecognized command \"" << input[0] << "\"" << std::endl;
	}

	//cleanup and return:
	//---------------
	delete encoder;
	delete outFile;

	return 0;
}