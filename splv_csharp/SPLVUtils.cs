/* SPLVutils.cs
 *
 * contains utility functions for using SpatialStudio with C#
 */

using System;
using System.Runtime.InteropServices;
using SPLVnative;
using Unity.Collections;
using UnityEngine;

//-------------------------------------------//

public enum SPLVaxis : Int32
{
	X = 0,
	Y = 1,
	Z = 2
}

//-------------------------------------------//

public static class SPLVutils
{
	public static IntPtr NativeArrayToSPLVframe(NativeArray<Vector4> colorData, Int32 xSize, Int32 ySize, Int32 zSize,
	                                            SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis)
	{
		//validate:
		//---------------
		if(xSize % SPLVbrick.SIZE != 0 || ySize % SPLVbrick.SIZE != 0 || zSize % SPLVbrick.SIZE != 0)
			throw new ArgumentException($"Frame dimensions must be multiples of SPLV_BRICK_SIZE ({SPLVbrick.SIZE})");

		if(colorData.Length != xSize * ySize * zSize)
			throw new ArgumentException("NativeArray size doesn't match provided dimensions");

		if(lrAxis == udAxis || lrAxis == fbAxis || udAxis == fbAxis)
			throw new ArgumentException("Axes must be distinct");

		//create frame:
		//---------------
		UInt32[] sizes = new UInt32[] { (UInt32)xSize, (UInt32)ySize, (UInt32)zSize };
		UInt32 widthMap  = sizes[(Int32)lrAxis] / SPLVbrick.SIZE;
		UInt32 heightMap = sizes[(Int32)udAxis] / SPLVbrick.SIZE;
		UInt32 depthMap  = sizes[(Int32)fbAxis] / SPLVbrick.SIZE;

		IntPtr frame = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(SPLVframe)));
		SPLVerror frameError = SPLV.splv_frame_create(frame, widthMap, heightMap, depthMap, 0);
		if(frameError != SPLVerror.SUCCESS)
			throw new Exception($"Error creating SPLV frame: ({frameError})");

		SPLVframe frameStruct = Marshal.PtrToStructure<SPLVframe>(frame);

		unchecked
		{
			for(UInt32 i = 0; i < widthMap * heightMap * depthMap; i++)
				Marshal.WriteInt32(frameStruct.map + (Int32)(i * sizeof(UInt32)), (Int32)SPLVframe.BRICK_IDX_EMPTY);
		}

		//read all voxels:
		//---------------
		for(Int32 x = 0; x < xSize; x++)
        for(Int32 y = 0; y < ySize; y++)
        for(Int32 z = 0; z < zSize; z++)
        {
            Int32 index = x + y * xSize + z * xSize * ySize;
            Vector4 color = colorData[index];

            if(color.w == 0)
                continue;

            Byte r = (Byte)(Mathf.Clamp01(color.x) * 255);
            Byte g = (Byte)(Mathf.Clamp01(color.y) * 255);
            Byte b = (Byte)(Mathf.Clamp01(color.z) * 255);

            UInt32[] readCoord = new UInt32[] { (UInt32)x, (UInt32)y, (UInt32)z };
            UInt32 xWrite = readCoord[(Int32)lrAxis];
            UInt32 yWrite = readCoord[(Int32)udAxis];
            UInt32 zWrite = readCoord[(Int32)fbAxis];

            UInt32 xMap   = xWrite / (UInt32)SPLVbrick.SIZE;
            UInt32 yMap   = yWrite / (UInt32)SPLVbrick.SIZE;
            UInt32 zMap   = zWrite / (UInt32)SPLVbrick.SIZE;
            UInt32 xBrick = xWrite % (UInt32)SPLVbrick.SIZE;
            UInt32 yBrick = yWrite % (UInt32)SPLVbrick.SIZE;
            UInt32 zBrick = zWrite % (UInt32)SPLVbrick.SIZE;

            UInt32 mapIdx = SPLV.splv_frame_get_map_idx(frame, xMap, yMap, zMap);
            
            UInt32 currentBrickIdx = (UInt32)Marshal.ReadInt32(frameStruct.map + (Int32)(mapIdx * sizeof(UInt32)));
            if(currentBrickIdx == SPLVframe.BRICK_IDX_EMPTY)
            {
                IntPtr newBrickPtr = SPLV.splv_frame_get_next_brick(frame);
                SPLV.splv_brick_clear(newBrickPtr);

                SPLVerror pushError = SPLV.splv_frame_push_next_brick(frame, xMap, yMap, zMap);
                if(pushError != SPLVerror.SUCCESS)
                    throw new Exception($"Failed to push brick to frame: ({pushError})");

                currentBrickIdx = (UInt32)Marshal.ReadInt32(frameStruct.map + (Int32)(mapIdx * sizeof(UInt32)));
            
				//underlying ptr may have changed, need to re-Marshal the struct
				frameStruct = Marshal.PtrToStructure<SPLVframe>(frame);
			}

            IntPtr brickPtr = frameStruct.bricks + (Int32)(currentBrickIdx * Marshal.SizeOf<SPLVbrick>());
            SPLV.splv_brick_set_voxel_filled(brickPtr, xBrick, yBrick, zBrick, r, g, b);
        }

        return frame;
	}
}