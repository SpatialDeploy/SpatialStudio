#include <iostream>
#include <sstream>
#include "math.h"
#include "splv_encoder.h"
#include "splv_vox_utils.h"
#include "splv_nvdb_utils.h"

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

void encode_frame(SPLVencoder* encoder, SPLVframe* frame, bool removeNonvisible)
{
	SPLVframe* processedFrame;
	if(removeNonvisible)
	{
		SPLVerror processingError = splv_frame_remove_nonvisible_voxels(frame, &processedFrame);
		if(processingError != SPLV_SUCCESS)
		{
			std::cout << "ERROR: failed to remove nonvisible voxels with code " <<
				processingError << " (" << splv_get_error_string(processingError) << ")\n";
			return;
		}
	}
	else
		processedFrame = frame;

	SPLVerror encodeError = splv_encoder_encode_frame(encoder, processedFrame);
	if(encodeError != SPLV_SUCCESS)
	{
		std::cout << processedFrame->width << ", " << processedFrame->height << ", " << processedFrame->depth << "\n";

		std::cout << "ERROR: failed to encode frame with code " 
			<< encodeError << " (" << splv_get_error_string(encodeError) << ")\n";
	}

	if(removeNonvisible)
		splv_frame_destroy(processedFrame);
}

//-------------------------------------------//

//usage: splv_encoder -d [xSize] [ySize] [zSize] -f [framerate] -o [output file]
int main(int argc, const char** argv)
{
	//parse and validate command line args:
	//---------------
	int32_t xSize = INT32_MAX;
	int32_t ySize = INT32_MAX;
	int32_t zSize = INT32_MAX;

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
	SPLVencoder* encoder;
	SPLVerror encoderError = splv_encoder_create(&encoder, xSize, ySize, zSize, framerate, outPath.c_str());
	if(encoderError != 	SPLV_SUCCESS)
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
	SPLVboundingBox boundingBox = { 0, 0, 0, xSize - 1, ySize - 1, zSize - 1 };

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

			SPLVframe* frame;
			SPLVerror nvdbError = splv_nvdb_load(((std::string)path).c_str(), &frame, &boundingBox, lrAxis, udAxis, fbAxis);
			if(nvdbError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to create nvdb frame with code " <<
					nvdbError << " (" << splv_get_error_string(nvdbError) << ")\n";
				continue;
			}

			encode_frame(encoder, frame, removeNonvisible);
			
			splv_frame_destroy(frame);
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
				encode_frame(encoder, frames[i], removeNonvisible);

			splv_vox_frames_destroy(frames, numFrames);
		}
		else if(command == "b")
		{
			int32_t newMinX, newMinY, newMinZ;
			int32_t newMaxX, newMaxY, newMaxZ;
			if (!(stream >> newMinX >> newMinY >> newMinZ >> newMaxX >> newMaxY >> newMaxZ)) 
			{
				std::cout << "ERROR: not enough coordinates specified for bounding box" << std::endl;
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
			std::string newLR, newUD, newFB;
			if(!(stream >> newLR >> newUD >> newFB))
			{
				std::cout << "ERROR: not enough axes specified for \"a\"" << std::endl;
				continue;
			}

			try
			{
				lrAxis = parse_axis(newLR);
				udAxis = parse_axis(newUD);
				fbAxis = parse_axis(newFB);
			}
			catch(std::exception e)
			{
				std::cout << "ERROR: invalid axes" << std::endl;
				return -1;
			}
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
				std::cout << "ERROR: invalud parameter given to \"r\" (expects \"on\" or \"off\")" << std::endl;
				continue;	
			}
		}
		else if(command == "f")
		{
			SPLVerror finishError = splv_encoder_finish(encoder);
			if(finishError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to finish encoding with code " << 
					finishError << " (" << splv_get_error_string(finishError) << ")\n";
			}

			break;
		}
		else if(command == "q")
		{
			splv_encoder_abort(encoder);
			break;
		}
		else
			std::cout << "ERROR: unrecognized command \"" << input[0] << "\"" << std::endl;
	}

	//return:
	//---------------
	return 0;
}