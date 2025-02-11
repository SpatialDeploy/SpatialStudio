#include <iostream>
#include <sstream>
#include <vector>
#include "math.h"
#include "spatialstudio/splv_encoder.h"
#include "spatialstudio/splv_vox_utils.h"
#include "spatialstudio/splv_nvdb_utils.h"

//-------------------------------------------//

class QuotedWord
{
public:
	operator std::string const& () const { return m_str; }

private:
	std::string m_str;

	friend std::istream& operator>>(std::istream& str, QuotedWord& value)
	{
		char x;
		str >> x;
        if((str) && (x == '"'))
        {
            std::string extra;
            std::getline(str, extra, '"');
            value.m_str = extra;
        }
        else
        {
            str.putback(x);
            str >> value.m_str;
        }

        return str;
      }
};

//-------------------------------------------//

//all currently active frames
static std::vector<SPLVframe> g_activeFrames;
static std::vector<std::pair<SPLVframe**, uint32_t>> g_activeVoxFrames;

//-------------------------------------------//

SPLVaxis parse_axis(std::string s)
{
	if(s == "x")
		return SPLV_AXIS_X;
	else if(s == "y")
		return SPLV_AXIS_Y;
	else if(s == "z")
		return SPLV_AXIS_Z;
	else
	throw std::invalid_argument("");
}

void free_frames()
{
	for(uint32_t i = 0; i < (uint32_t)g_activeFrames.size(); i++)
		splv_frame_destroy(&g_activeFrames[i]);
	g_activeFrames.clear();

	for(uint32_t i = 0; i < (uint32_t)g_activeVoxFrames.size(); i++)
		splv_vox_frames_destroy(g_activeVoxFrames[i].first, g_activeVoxFrames[i].second);
	g_activeVoxFrames.clear();	
}

void encode_frame(SPLVencoder* encoder, SPLVframe* frame, bool removeNonvisible)
{
	//validate:
	//---------------
	if(frame->width  * SPLV_BRICK_SIZE != encoder->width  || 
	   frame->height * SPLV_BRICK_SIZE != encoder->height || 
	   frame->depth  * SPLV_BRICK_SIZE != encoder->depth)
	{
		std::cout << "ERROR: frame dimensions do not match encoder dimensions" << std::endl;
		return;
	}

	//preprocess frame:
	//---------------
	SPLVframe processedFrame;
	if(removeNonvisible)
	{
		SPLVerror processingError = splv_frame_remove_nonvisible_voxels(frame, &processedFrame);
		if(processingError != SPLV_SUCCESS)
		{
			std::cout << "ERROR: failed to remove nonvisible voxels with code " <<
				processingError << " (" << splv_get_error_string(processingError) << ")\n";
			return;
		}

		g_activeFrames.push_back(processedFrame);
		frame = &processedFrame;
	}

	//encode:
	//---------------
	splv_bool_t canRemove;
	SPLVerror encodeError = splv_encoder_encode_frame(encoder, frame, &canRemove);
	if(encodeError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to encode frame with code " 
			<< encodeError << " (" << splv_get_error_string(encodeError) << ")\n";
	}

	//free active frames:
	//---------------
	if(canRemove)
		free_frames();
}

//-------------------------------------------//

//usage: splv_encoder -d [width] [height] [depth] -f [framerate] -o [output file]
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
		else if(arg == "-g")
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
		else if(arg == "-b")
		{
			if(i + 1 >= (uint32_t)argc)
			{
				std::cout << "ERROR: not enough arguments supplied to \"-r\"" << std::endl;
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
				std::cout << "ERROR: invalid maximum region size" << std::endl;
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
			std::cout << "VALID USAGE: splv_encoder -d [width] [height] [depth] -f [framerate] -o [output file]" << std::endl;
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

	if(outPath == "")
	{
		std::cout << "ERROR: no output file specified (use \"-o [output file]\")" << std::endl;
		return -1;
	}

	//create outfile and encoder:
	//---------------
	SPLVencodingParams encodingParams;
	encodingParams.gopSize = gopSize;
	encodingParams.maxBrickGroupSize = maxBrickGroupSize;

	SPLVencoder encoder;
	SPLVerror encoderError = splv_encoder_create(&encoder, width, height, depth, framerate, encodingParams, outPath.c_str());
	if(encoderError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create encoder with error code " << 
			encoderError << "(" << splv_get_error_string(encoderError) << ")\n";
		return -1;
	}

	//print welcome message:
	//---------------
	std::cout << "===================================" << std::endl;
	std::cout << "            SPLV Encoder           " << std::endl;
	std::cout << "===================================" << std::endl;
	std::cout << "- \"e_nvdb [path/to/nvdb]\"" << std::endl;
	std::cout << "- \"e_vox [path/to/vox]\"" << std::endl;
	std::cout << "- \"b [minX] [minY] [minZ] [maxX] [maxY] [maxZ]\" to set the bounding box of all subsequent frames" << std::endl;
	std::cout << "- \"r [on/off]\" to enable/disable removal of nonvisible voxels for all subsequent frames (increases encoding time)" << std::endl;
	std::cout << "- \"a [lr axis] [ud axis] [fb axis]\" to set the axes corresponding to the cardinal directions for all subsequent nvdb fraes" << std::endl;
	std::cout << "- \"f\" to finish encoding and exit program" << std::endl;
	std::cout << "- \"q\" to exit program without finishing encoding" << std::endl;
	std::cout << std::endl;

	//check for commands in loop:
	//---------------
	SPLVboundingBox boundingBox = { 0, 0, 0, width - 1, height - 1, depth - 1 };

	SPLVaxis lrAxis = SPLV_AXIS_X;
	SPLVaxis udAxis = SPLV_AXIS_Y;
	SPLVaxis fbAxis = SPLV_AXIS_Z;

	bool removeNonvisible = false;

	while(true)
	{
		std::cout << "> ";

		std::string input;
		std::getline(std::cin, input);
		std::istringstream stream(input);

		std::string command;
		if(!(stream >> command))
			continue;

		if(command == "e_nvdb")
		{
			QuotedWord path;
			if(!(stream >> path))
			{
				std::cout << "ERROR: no NVDB file specified" << std::endl;
				continue;
			}

			SPLVframe frame;
			SPLVerror nvdbError = splv_nvdb_load(((std::string)path).c_str(), &frame, &boundingBox, lrAxis, udAxis, fbAxis);
			if(nvdbError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to create nvdb frame with code " <<
					nvdbError << " (" << splv_get_error_string(nvdbError) << ")\n";
				continue;
			}

			g_activeFrames.push_back(frame);
			encode_frame(&encoder, &frame, removeNonvisible);
		}
		else if(command == "e_vox")
		{
			QuotedWord path;
			if(!(stream >> path))
			{
				std::cout << "ERROR: no VOX file specified" << std::endl;
				continue;
			}

			uint32_t numFrames;
			SPLVframe** frames;
			SPLVerror voxError = splv_vox_load(((std::string)path).c_str(), &frames, &numFrames, &boundingBox);
			if(voxError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to create vox frames with code " <<
					voxError << " (" << splv_get_error_string(voxError) << ")\n";
				continue;
			}

			for(uint32_t i = 0; i < numFrames; i++)
				encode_frame(&encoder, frames[i], removeNonvisible);

			g_activeVoxFrames.push_back({ frames, numFrames });
		}
		else if(command == "b")
		{
			int32_t newMinX, newMinY, newMinZ;
			int32_t newMaxX, newMaxY, newMaxZ;
			if(!(stream >> newMinX >> newMinY >> newMinZ >> newMaxX >> newMaxY >> newMaxZ)) 
			{
				std::cout << "ERROR: not enough coordinates specified for bounding box" << std::endl;
				continue;
			}

			int32_t newXsize = newMaxX - newMinX + 1;
			int32_t newYsize = newMaxY - newMinY + 1;
			int32_t newZsize = newMaxZ - newMinZ + 1;
			if(newXsize <= 0 || newYsize <= 0 || newZsize <= 0)
			{
				std::cout << "ERROR: bounding box dimensions must be positive" << std::endl;
				continue;
			}			

			if(newXsize % SPLV_BRICK_SIZE != 0 || newYsize % SPLV_BRICK_SIZE != 0 || newZsize % SPLV_BRICK_SIZE != 0)
			{
				std::cout << "ERROR: bounding box dimensions must be multiples of SPLV_BRICK_SIZE" << std::endl;
				continue;
			}

			boundingBox.xMin = newMinX;
			boundingBox.yMin = newMinY;
			boundingBox.zMin = newMinZ;
			boundingBox.xMax = newMaxX;
			boundingBox.yMax = newMaxY;
			boundingBox.zMax = newMaxZ;
		}
		else if(command == "a")
		{
			std::string newLRstr, newUDstr, newFBstr;
			if(!(stream >> newLRstr >> newUDstr >> newFBstr))
			{
				std::cout << "ERROR: not enough axes specified for \"a\"" << std::endl;
				continue;
			}

			SPLVaxis newLR;
			SPLVaxis newUD;
			SPLVaxis newFB;
			try
			{
				newLR = parse_axis(newLRstr);
				newUD = parse_axis(newUDstr);
				newFB = parse_axis(newFBstr);
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid axes" << std::endl;
				continue;
			}

			if(newLR == newUD || newLR == newFB || newUD == newFB)
			{
				std::cout << "ERROR: axes must be distinct" << std::endl;
				continue;
			}

			lrAxis = newLR;
			udAxis = newUD;
			fbAxis = newFB;
		}
		else if(command == "r")
		{
			std::string option;
			if(!(stream >> option))
			{
				std::cout << "ERROR: no parameter given to \"r\"" << std::endl;
				continue;
			}

			if(option == "on")
				removeNonvisible = true;
			else if(option == "off")
				removeNonvisible = false;
			else
			{
				std::cout << "ERROR: invalid parameter given to \"r\" (expects \"on\" or \"off\")" << std::endl;
				continue;	
			}
		}
		else if(command == "f")
		{
			SPLVerror finishError = splv_encoder_finish(&encoder);
			if(finishError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to finish encoding with code " << 
					finishError << " (" << splv_get_error_string(finishError) << ")\n";
			}

			free_frames();
			break;
		}
		else if(command == "q")
		{
			splv_encoder_abort(&encoder);

			free_frames();
			break;
		}
		else
			std::cout << "ERROR: unrecognized command \"" << input[0] << "\"" << std::endl;
	}

	//return:
	//---------------
	return 0;
}