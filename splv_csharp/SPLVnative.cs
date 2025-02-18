/* SPLVnative.cs
 * 
 * contains all bindings from the SPLV C++ library
 */

using System;
using System.Runtime.InteropServices;

//-------------------------------------------//

namespace SPLVnative
{

public enum SPLVerror : Int32
{
	SUCCESS = 0,
	ERROR_INVALID_ARGUMENTS,
	ERROR_INVALID_INPUT,
	ERROR_OUT_OF_MEMORY,
	ERROR_FILE_OPEN,
	ERROR_FILE_READ,
	ERROR_FILE_WRITE,
	ERROR_RUNTIME
};

//-------------------------------------------//

[StructLayout(LayoutKind.Sequential)]
public struct SPLVbrick
{
	public const uint SIZE = 8;
	public const uint LEN = SIZE * SIZE * SIZE;

	[MarshalAs(UnmanagedType.ByValArray, SizeConst = (int)LEN / 32)]
	public UInt32[] bitmap;
	
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = (int)LEN)]
	public UInt32[] color;
}

[StructLayout(LayoutKind.Sequential)]
public struct SPLVdynArrayUint64
{
	public UInt64 cap;
	public IntPtr arr;

	public UInt64 len;
}

[StructLayout(LayoutKind.Sequential)]
public struct SPLVframe
{
	public const UInt32 BRICK_IDX_EMPTY = UInt32.MaxValue;

	public UInt32 width;
	public UInt32 height;
	public UInt32 depth;
	public IntPtr map;

	public UInt32 bricksLen;
	public UInt32 bricksCap;
	public IntPtr bricks;
}

[StructLayout(LayoutKind.Sequential)]
public struct SPLVencodingParams
{
	public UInt32 gopSize;
	public UInt32 maxBrickGroupSize;
}

[StructLayout(LayoutKind.Sequential)]
public struct SPLVencoder
{
	public UInt32 width;
	public UInt32 height;
	public UInt32 depth;

	public float framerate;
	public UInt32 frameCount;
	public SPLVdynArrayUint64 frameTable;

	public SPLVencodingParams encodingParams;

	public SPLVframe lastFrame;

	public IntPtr outFile;

	public UInt64 mapBitmapLen;
	public IntPtr scratchBufMapBitmap;
	public IntPtr scratchBufBricks;
	public IntPtr scratchBufBrickPositions;
	public IntPtr scratchBufBrickGroupWriters;
}

//-------------------------------------------//

public class SPLV
{
	private const string LibraryName = "splv_encoder_shared";

	//-------------------------------------------//
	//from splv_brick.h

	[DllImport(LibraryName, EntryPoint = "splv_brick_set_voxel_filled", CallingConvention = CallingConvention.Cdecl)]
	public static extern void BrickSetVoxelFilled(IntPtr brick, UInt32 x, UInt32 y, UInt32 z, Byte colorR, Byte colorG, Byte colorB);

	[DllImport(LibraryName, EntryPoint = "splv_brick_set_voxel_empty", CallingConvention = CallingConvention.Cdecl)]
	public static extern void BrickSetVoxelEmpty(IntPtr brick, UInt32 x, UInt32 y, UInt32 z);

	[DllImport(LibraryName, EntryPoint = "splv_brick_get_voxel", CallingConvention = CallingConvention.Cdecl)]
	public static extern Byte BrickGetVoxel(IntPtr brick, UInt32 x, UInt32 y, UInt32 z);

	[DllImport(LibraryName, EntryPoint = "splv_brick_get_voxel_color", CallingConvention = CallingConvention.Cdecl)]
	public static extern Byte BrickGetVoxelColor(IntPtr brick, UInt32 x, UInt32 y, UInt32 z, out Byte colorR, out Byte colorG, out Byte colorB);

	[DllImport(LibraryName, EntryPoint = "splv_brick_clear", CallingConvention = CallingConvention.Cdecl)]
	public static extern void BrickClear(IntPtr brick);

	//-------------------------------------------//
	//from splv_frame.h

	[DllImport(LibraryName, EntryPoint = "splv_frame_create", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror FrameCreate(IntPtr frame, UInt32 width, UInt32 height, UInt32 depth, UInt32 numBricksInitial);

	[DllImport(LibraryName, EntryPoint = "splv_frame_destroy", CallingConvention = CallingConvention.Cdecl)]
	public static extern void FrameDestroy(IntPtr frame);

	[DllImport(LibraryName, EntryPoint = "splv_frame_get_map_idx", CallingConvention = CallingConvention.Cdecl)]
	public static extern UInt32 FrameGetMapIdx(IntPtr frame, UInt32 x, UInt32 y, UInt32 z);

	[DllImport(LibraryName, EntryPoint = "splv_frame_get_next_brick", CallingConvention = CallingConvention.Cdecl)]
	public static extern IntPtr FrameGetNextBrick(IntPtr frame);

	[DllImport(LibraryName, EntryPoint = "splv_frame_push_next_brick", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror FramePushNextBrick(IntPtr frame, UInt32 x, UInt32 y, UInt32 z);

	[DllImport(LibraryName, EntryPoint = "splv_frame_remove_nonvisible_voxels", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror FrameRemoveNonvisibleVoxels(IntPtr frame, IntPtr processedFrame);
	
	//-------------------------------------------//
	//from splv_encoder.h

	[DllImport(LibraryName, EntryPoint = "splv_encoder_create", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror EncoderCreate(IntPtr encoder, UInt32 width, UInt32 height, UInt32 depth, float framerate, SPLVencodingParams encodingParams, IntPtr outPath);

	[DllImport(LibraryName, EntryPoint = "splv_encoder_encode_frame", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror EncoderEncodeFrame(IntPtr encoder, IntPtr frame, out Byte canFree);

	[DllImport(LibraryName, EntryPoint = "splv_encoder_finish", CallingConvention = CallingConvention.Cdecl)]
	public static extern SPLVerror EncoderFinish(IntPtr encoder);

	[DllImport(LibraryName, EntryPoint = "splv_encoder_abort", CallingConvention = CallingConvention.Cdecl)]
	public static extern void EncoderAbort(IntPtr encoder);
}

} //namespace SpatialStudioNative