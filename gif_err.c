/*****************************************************************************

gif_err.c - handle error reporting for the GIF library.

****************************************************************************/
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 1989 Gershon Elber

#include "gif_lib.h"

/*****************************************************************************
 Return a string description of  the last GIF error
*****************************************************************************/
const char *GifErrorString(int ErrorCode) {
	const char *Err;

	switch (ErrorCode) {
	case D_GIF_ERR_READ_FAILED:
		Err = "Failed to read from given file";
		break;
	case D_GIF_ERR_NOT_GIF_FILE:
		Err = "Data is not in GIF format";
		break;
	case D_GIF_ERR_NO_SCRN_DSCR:
		Err = "No screen descriptor detected";
		break;
	case D_GIF_ERR_NO_IMAG_DSCR:
		Err = "No Image Descriptor detected";
		break;
	case D_GIF_ERR_NO_COLOR_MAP:
		Err = "Neither global nor local color map";
		break;
	case D_GIF_ERR_WRONG_RECORD:
		Err = "Wrong record type detected";
		break;
	case D_GIF_ERR_DATA_TOO_BIG:
		Err = "Number of pixels bigger than width * height";
		break;
	case D_GIF_ERR_NOT_ENOUGH_MEM:
		Err = "Failed to allocate required memory";
		break;
	case D_GIF_ERR_NOT_READABLE:
		Err = "Given file was not opened for read";
		break;
	case D_GIF_ERR_IMAGE_DEFECT:
		Err = "Image is defective, decoding aborted";
		break;
	case D_GIF_ERR_EOF_TOO_SOON:
		Err = "Image EOF detected before image complete";
		break;
	default:
		Err = NULL;
		break;
	}
	return Err;
}

/* end */
