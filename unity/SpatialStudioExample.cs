/* SpatialStudioExample.cs
 *
 * contains example code to encode a basic SPlV
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Unity.Collections;
using UnityEngine;

public class SpatialStudioExample : MonoBehaviour
{
	private const int EXAMPLE_FRAME_SIZE = 16;

	void Start()
	{
		ExampleEncode();
	}

	void ExampleEncode()
	{
		//create encoder:
		//---------------
		IntPtr outPath = Marshal.StringToHGlobalAnsi("C:\\Users\\danie\\Downloads\\test.splv");

		IntPtr encoder = IntPtr.Zero;
		SPLVerror encoderError = SpatialStudio.splv_encoder_create(ref encoder, EXAMPLE_FRAME_SIZE, EXAMPLE_FRAME_SIZE, EXAMPLE_FRAME_SIZE, 1.0f, 1, outPath);
		if(encoderError != SPLVerror.SUCCESS)
			throw new Exception($"Failed to create SPLVencoder: ({encoderError})");

		//encode frames:
		//---------------
		List<IntPtr> activeFrames = new List<IntPtr>();

		for(int i = 0; i < EXAMPLE_FRAME_SIZE; i++)
		{
			IntPtr frame = CreateExampleFrame(i);
			activeFrames.Add(frame);

			Byte canFree;
			SPLVerror encodeError = SpatialStudio.splv_encoder_encode_frame(encoder, frame, out canFree);
			if(encodeError != SPLVerror.SUCCESS)
				throw new Exception($"Failed to encode frame: ({encodeError})");

			if(canFree != 0)
			{
				foreach(var activeFrame in activeFrames)
					SpatialStudio.splv_frame_destroy(activeFrame);

				activeFrames.Clear();
			}
		}

		//finish encoding, free remaining active frames:
		//---------------
		SPLVerror finishError = SpatialStudio.splv_encoder_finish(encoder);
		if(finishError != SPLVerror.SUCCESS)
			throw new Exception($"Failed to finish encoding: ({finishError})");

		foreach(var activeFrame in activeFrames)
			SpatialStudio.splv_frame_destroy(activeFrame);

		activeFrames.Clear();
	}

	private IntPtr CreateExampleFrame(int idx)
	{
		NativeArray<Vector4> arr = new NativeArray<Vector4>(
			EXAMPLE_FRAME_SIZE * EXAMPLE_FRAME_SIZE * EXAMPLE_FRAME_SIZE, 
			Allocator.Temp
		);

		for(int i = 0; i < EXAMPLE_FRAME_SIZE * EXAMPLE_FRAME_SIZE * EXAMPLE_FRAME_SIZE; i++)
			arr[i] = Vector4.zero;

		int x = idx;
		int y = EXAMPLE_FRAME_SIZE / 2;
		int z = EXAMPLE_FRAME_SIZE / 2;
		arr[x + EXAMPLE_FRAME_SIZE * (y + EXAMPLE_FRAME_SIZE * z)] = new Vector4(1.0f, 1.0f, 1.0f, 1.0f);

		IntPtr frame = SpatialStudioUtils.NativeArrayToSPLVframe(
			arr, 
			EXAMPLE_FRAME_SIZE, 
			EXAMPLE_FRAME_SIZE,
			EXAMPLE_FRAME_SIZE,
			SPLVaxis.X, SPLVaxis.Y, SPLVaxis.Z
		);

		arr.Dispose();

		return frame;
	}
}
