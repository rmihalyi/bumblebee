//*****************************************************************************
// Contrast Limited Adaptive Histogram Equalization (CLAHE) for OpenCV
//-----------------------------------------------------------------------------
// Original CLAHE implementation by Karel Zuiderveld, karel@cv.ruu.nl
// in "Graphics Gems IV", Academic Press, 1994.
//-----------------------------------------------------------------------------
// Converted to OpenCV format by Toby Breckon, toby.breckon@cranfield.ac.uk
// Copyright (c) 2009 School of Engineering, Cranfield University
// License : LGPL - http://www.gnu.org/licenses/lgpl.html
//-----------------------------------------------------------------------------
// Improved by Shervin Emami on 17th Nov 2010, shervin.emami@gmail.com
// http://www.shervinemami.co.cc/
//*****************************************************************************
#ifndef __CLAHE_HEADER__
#define __CLAHE_HEADER__

//#include <cv.h>
// OpenCV
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/video/video.hpp"
#include "opencv2/gpu/gpu.hpp"
#include "opencv2/core/core.hpp"

#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/imgproc/imgproc_c.h"
#include "opencv2/core/internal.hpp"

using namespace cv;



// *****************************************************************************

// CLAHE input/output range flag definitions

#define CV_CLAHE_RANGE_FULL 0
#define CV_CLAHE_RANGE_INPUT 1

/// *****************************************************************************

void cvAdaptEqualize(const CvArr* src, CvArr* dst,
                        unsigned int xdivs, unsigned int ydivs, unsigned int bins, int range);



// cvAdaptEqualize(src, dst, xdivs, ydivs, bins)
//
// src - pointer to source image (must be single channel 8-bit)
// dst - pointer to destination image (must be single channel 8-bit)
// xdivs - number of cell divisions to use in vertical (x) direction (MIN=2 MAX = 16)
// ydivs - number of cell divisions to use in vertical (y) direction (MIN=2 MAX = 16)
// bins - number of histogram bins to use per division
// range - either of CV_CLAHE_RANGE_INPUT or CV_CLAHE_RANGE_FULL to limit the output
//         pixel range to the min/max range of the input image or the full range of the
//         pixel depth (8-bit in this case)

// cvCLAdaptEqualize(src, dst, xdivs, ydivs, bins, limit)
//
// src - pointer to source image (must be single channel 8-bit)
// dst - pointer to destination image (must be single channel 8-bit)
// xdivs - number of cell divisions to use in vertical (x) direction (MIN=2 MAX = 16)
// ydivs - number of cell divisions to use in vertical (y) direction (MIN=2 MAX = 16)
// bins - number of histogram bins to use per division
// limit - contrast limit for localised changes in contrast
// range - either of CV_CLAHE_RANGE_INPUT or CV_CLAHE_RANGE_FULL to limit the output
//         pixel range to the min/max range of the input image or the full range of the
//         pixel depth (8-bit in this case)


void cvCLAdaptEqualize(const CvArr* src, CvArr* dst,
                        unsigned int xdivs, unsigned int ydivs, unsigned int bins, float limit,
                        int range);
       
// *****************************************************************************

#undef CV_ERROR
#define CV_ERROR( Code, Msg )                                       \
{                                                                   \
     cvError( (Code), "CLAHE code", Msg, __FILE__, __LINE__ );       \
     exit(1);                                                          \
}
                       
// *****************************************************************************

#endif
