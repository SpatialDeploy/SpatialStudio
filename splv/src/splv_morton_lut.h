/**
 * splv_morton_lut.h
 * 
 * contains a lookup table to go from xyz -> morton order coordinates
 */

 #ifndef SPLV_MORTON_LUT_H
 #define SPLV_MORTON_LUT_H
 
 #include <stdint.h>
 #include "spatialstudio/splv_brick.h"
 
 //-------------------------------------------//
 
 /**
  * an xyz coordinate
  */
 typedef struct SPLVmortonCoordinate
 {
	 uint8_t x;
	 uint8_t y;
	 uint8_t z;
 } SPLVmortonCoordinate;
 
 static const uint32_t MORTON_TO_IDX[SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE] = {
	 0, 1, 8, 9, 64, 65, 72, 73, 2, 3, 10, 11, 66, 67, 74, 75, 16, 17, 24, 25, 80, 81, 88, 89, 18, 19, 26, 27, 82, 83, 90, 91,
	 128, 129, 136, 137, 192, 193, 200, 201, 130, 131, 138, 139, 194, 195, 202, 203, 144, 145, 152, 153, 208, 209, 216, 217, 146, 147, 154, 155, 210, 211, 218, 219,
	 4, 5, 12, 13, 68, 69, 76, 77, 6, 7, 14, 15, 70, 71, 78, 79, 20, 21, 28, 29, 84, 85, 92, 93, 22, 23, 30, 31, 86, 87, 94, 95,
	 132, 133, 140, 141, 196, 197, 204, 205, 134, 135, 142, 143, 198, 199, 206, 207, 148, 149, 156, 157, 212, 213, 220, 221, 150, 151, 158, 159, 214, 215, 222, 223,
	 32, 33, 40, 41, 96, 97, 104, 105, 34, 35, 42, 43, 98, 99, 106, 107, 48, 49, 56, 57, 112, 113, 120, 121, 50, 51, 58, 59, 114, 115, 122, 123,
	 160, 161, 168, 169, 224, 225, 232, 233, 162, 163, 170, 171, 226, 227, 234, 235, 176, 177, 184, 185, 240, 241, 248, 249, 178, 179, 186, 187, 242, 243, 250, 251,
	 36, 37, 44, 45, 100, 101, 108, 109, 38, 39, 46, 47, 102, 103, 110, 111, 52, 53, 60, 61, 116, 117, 124, 125, 54, 55, 62, 63, 118, 119, 126, 127,
	 164, 165, 172, 173, 228, 229, 236, 237, 166, 167, 174, 175, 230, 231, 238, 239, 180, 181, 188, 189, 244, 245, 252, 253, 182, 183, 190, 191, 246, 247, 254, 255,
	 256, 257, 264, 265, 320, 321, 328, 329, 258, 259, 266, 267, 322, 323, 330, 331, 272, 273, 280, 281, 336, 337, 344, 345, 274, 275, 282, 283, 338, 339, 346, 347,
	 384, 385, 392, 393, 448, 449, 456, 457, 386, 387, 394, 395, 450, 451, 458, 459, 400, 401, 408, 409, 464, 465, 472, 473, 402, 403, 410, 411, 466, 467, 474, 475,
	 260, 261, 268, 269, 324, 325, 332, 333, 262, 263, 270, 271, 326, 327, 334, 335, 276, 277, 284, 285, 340, 341, 348, 349, 278, 279, 286, 287, 342, 343, 350, 351,
	 388, 389, 396, 397, 452, 453, 460, 461, 390, 391, 398, 399, 454, 455, 462, 463, 404, 405, 412, 413, 468, 469, 476, 477, 406, 407, 414, 415, 470, 471, 478, 479,
	 288, 289, 296, 297, 352, 353, 360, 361, 290, 291, 298, 299, 354, 355, 362, 363, 304, 305, 312, 313, 368, 369, 376, 377, 306, 307, 314, 315, 370, 371, 378, 379,
	 416, 417, 424, 425, 480, 481, 488, 489, 418, 419, 426, 427, 482, 483, 490, 491, 432, 433, 440, 441, 496, 497, 504, 505, 434, 435, 442, 443, 498, 499, 506, 507,
	 292, 293, 300, 301, 356, 357, 364, 365, 294, 295, 302, 303, 358, 359, 366, 367, 308, 309, 316, 317, 372, 373, 380, 381, 310, 311, 318, 319, 374, 375, 382, 383,
	 420, 421, 428, 429, 484, 485, 492, 493, 422, 423, 430, 431, 486, 487, 494, 495, 436, 437, 444, 445, 500, 501, 508, 509, 438, 439, 446, 447, 502, 503, 510, 511
 };
 
 #endif //#ifndef MORTON_LUT_H