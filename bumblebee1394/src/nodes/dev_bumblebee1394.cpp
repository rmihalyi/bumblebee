///////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2009, 2010 Patrick Beeson, Jack O'Quin, Ken Tossell
//  ROS port of the Player 1394 camera driver.
//
// Copyright (C) 2004 Nate Koenig, Andrew Howard
//  Player driver for IEEE 1394 digital camera capture
//
// Copyright (C) 2000-2003 Damien Douxchamps, Dan Dennedy
//  Bayer filtering from libdc1394
//
// NOTE: On 4 Jan. 2011, this file was re-licensed under the GNU LGPL
// with permission of the original GPL authors: Nate Koenig, Andrew
// Howard, Damien Douxchamps and Dan Dennedy.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2 of
// the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
///////////////////////////////////////////////////////////////////////////

// $Id: dev_camera1394.cpp 37636 2011-07-21 00:45:20Z joq $

/** @file

    @brief libdc1394 digital camera library interface implementation
 
    This device interface is partly derived from the Player 1394
    camera driver.

    The ROS image pipeline provides Bayer filtering at a higher level
    (in image_proc).  In some cases it is useful to run the driver
    without the entire image pipeline, so libdc1394 Bayer decoding is
    also provided here.

 */

#include <stdint.h>
#include "yuv.h"
#include <sensor_msgs/image_encodings.h>
#include "dev_bumblebee1394.h"
#include "features.h"
#include "modes.h"

#define NUM_DMA_BUFFERS 8

// @todo eliminate these macros
//! Macro for throwing an exception with a message
#define CAM_EXCEPT(except, msg)					\
  {								\
    char buf[100];						\
    snprintf(buf, 100, "[Camera1394::%s]: " msg, __FUNCTION__); \
    throw except(buf);						\
  }

//! Macro for throwing an exception with a message, passing args
#define CAM_EXCEPT_ARGS(except, msg, ...)				\
  {									\
    char buf[100];							\
    snprintf(buf, 100, "[Camera1394::%s]: " msg, __FUNCTION__, __VA_ARGS__); \
    throw except(buf);							\
  }


using namespace bumblebee1394;

////////////////////////////////////////////////////////////////////////////////
// Constructor
Bumblebee1394::Bumblebee1394():
  camera_(NULL)
{}

Bumblebee1394::~Bumblebee1394() 
{
  SafeCleanup();
}

void Bumblebee1394::findBayerPattern(const char* bayer)
{
  // determine Bayer color encoding pattern
  // (default is different from any color filter provided by DC1394)
  BayerPattern_ = (dc1394color_filter_t) DC1394_COLOR_FILTER_NUM;
  if (0 == strcmp(bayer, "bggr"))
    {
      BayerPattern_ = DC1394_COLOR_FILTER_BGGR;
    }
  else if (0 == strcmp(bayer, "grbg"))
    {
      BayerPattern_ = DC1394_COLOR_FILTER_GRBG;
    }
  else if (0 == strcmp(bayer, "rggb"))
    {
      BayerPattern_ = DC1394_COLOR_FILTER_RGGB;
    }
  else if (0 == strcmp(bayer, "gbrg"))
    {
      BayerPattern_ = DC1394_COLOR_FILTER_GBRG;
    }
  else if (0 != strcmp(bayer, ""))
    {
      ROS_ERROR("unknown bayer pattern [%s]", bayer);
    }
}

bool Bumblebee1394::findBayerMethod(const char* method)
{
  // Do Bayer conversion in the driver node?
  bool DoBayer = false;                 // return value
  if (0 != strcmp(method, "")
      && BayerPattern_ != DC1394_COLOR_FILTER_NUM)
    {
      DoBayer = true;                   // decoding in driver
      // add method name to message:
      ROS_WARN("[%s] Bayer decoding in the driver is DEPRECATED;"
               " image_proc decoding preferred.", method);

      // Set decoding method
      if (!strcmp(method, "DownSample"))
        BayerMethod_ = DC1394_BAYER_METHOD_DOWNSAMPLE;
      else if (!strcmp(method, "Simple"))
        BayerMethod_ = DC1394_BAYER_METHOD_SIMPLE;
      else if (!strcmp(method, "Bilinear"))
        BayerMethod_ = DC1394_BAYER_METHOD_BILINEAR;
      else if (!strcmp(method, "Edge"))
        BayerMethod_ = DC1394_BAYER_METHOD_EDGESENSE;
      else if (!strcmp(method, "HQ"))
        BayerMethod_ = DC1394_BAYER_METHOD_HQLINEAR;
      else if (!strcmp(method, "VNG"))
        BayerMethod_ = DC1394_BAYER_METHOD_VNG;
      else if (!strcmp(method, "AHD"))
        BayerMethod_ = DC1394_BAYER_METHOD_AHD;
      else
        {
          ROS_ERROR("Unknown Bayer method [%s]. Using ROS image_proc instead.",
                    method);
          DoBayer = false;
        }
    }
  return DoBayer;
}

/** Open the 1394 device and start streaming
 *
 *  @param newconfig new configuration parameters
 *  @return 0 if successful
 *
 *  TODO (if successful):
 *     * update newconfig.guid
 *     * validate newconfig.video_mode
 *     * initialize Features class
 */
int Bumblebee1394::open(bumblebee1394::Bumblebee1394Config &newconfig)
{
  //////////////////////////////////////////////////////////////
  // First, look for the camera
  //////////////////////////////////////////////////////////////

  const char *guid = newconfig.guid.c_str();  // C-style GUID for libdc1394
  int err;
  dc1394_t *d;
  dc1394camera_list_t *list;

  // TODO: make error exit paths clean up resources properly
  d = dc1394_new ();
  if (d == NULL)
    {
      CAM_EXCEPT(bumblebee1394::Exception,
                 "Could not initialize dc1394_context.\n"
                 "Make sure /dev/raw1394 exists, you have access permission,\n"
                 "and libraw1394 development package is installed.");
    }

  err = dc1394_camera_enumerate(d, &list);
  if (err != DC1394_SUCCESS)
    {
      CAM_EXCEPT(bumblebee1394::Exception, "Could not get camera list");
      return -1;
    }
  
  if (list->num == 0)
    {
      CAM_EXCEPT(bumblebee1394::Exception, "No cameras found");
      return -1;
    }
  
  char* temp=(char*)malloc(1024*sizeof(char));
  for (unsigned i=0; i < list->num; i++)
    {
      // Create a camera
      camera_ = dc1394_camera_new (d, list->ids[i].guid);
      if (!camera_)
	ROS_WARN_STREAM("Failed to initialize camera with GUID "
                        << std::hex << list->ids[i].guid);
      else
        ROS_INFO_STREAM("Found camera with GUID "
                        << std::hex << list->ids[i].guid);

      uint32_t value[3];
      
      value[0]= camera_->guid & 0xffffffff;
      value[1]= (camera_->guid >>32) & 0x000000ff;
      value[2]= (camera_->guid >>40) & 0xfffff;
      
      sprintf(temp,"%06x%02x%08x", value[2], value[1], value[0]);

      if (strcmp(guid,"")==0)
        {
          ROS_INFO_STREAM("No guid specified, using first camera found, GUID: "
                          << std::hex << camera_->guid);
          device_id_ = std::string(temp);
          break;
        }

      ROS_DEBUG("Comparing %s to %s",guid,temp);
      if (strcmp(temp,guid)==0)
        {
          device_id_=guid;
          break;
        }
      SafeCleanup();
    }
  free (temp);
  dc1394_camera_free_list (list);
  
  if (!camera_)
    {
      if (strcmp(guid,"")==0)
        { 
          CAM_EXCEPT(bumblebee1394::Exception, "Could not find camera");
        }
      else
        {
          CAM_EXCEPT_ARGS(bumblebee1394::Exception,
                          "Could not find camera with guid %s", guid);
        }
      return -1;
    }

  ROS_INFO_STREAM("camera model: " << camera_->vendor
                  << " " << camera_->model);

  //////////////////////////////////////////////////////////////
  // initialize camera
  //////////////////////////////////////////////////////////////

  // resetting some cameras is not a good idea
  if (newconfig.reset_on_open
      && DC1394_SUCCESS != dc1394_camera_reset(camera_))
    {
      // reset failed: log a warning, but continue
      ROS_WARN("Unable to reset camera (continuing).");
    }

  // first, set parameters that are common between Format7 and other modes
  if (false == Modes::setIsoSpeed(camera_, newconfig.iso_speed))
    {
      SafeCleanup();
      CAM_EXCEPT(bumblebee1394::Exception,
                 "Unable to set ISO speed; is the camera plugged in?");
      return -1;
    }

  // set video mode
  videoMode_ = Modes::getVideoMode(camera_, newconfig.video_mode);
  if (DC1394_SUCCESS != dc1394_video_set_mode(camera_, videoMode_))
    {
      SafeCleanup();
      CAM_EXCEPT(bumblebee1394::Exception, "Failed to set video mode");
      return -1;
    }

  multicam_ = newconfig.multicam;

  //////////////////////////////////////////////////////////////
  // special handling for Format7 modes
  //////////////////////////////////////////////////////////////

  DoBayerConversion_ = false;

  if (dc1394_is_video_mode_scalable(videoMode_) == DC1394_TRUE)
    {
      // set Format7 parameters
      if (!format7_.start(camera_, videoMode_, newconfig))
        {
          SafeCleanup();
          CAM_EXCEPT(bumblebee1394::Exception, "Format7 start failed");
          return -1;
        }
    }
  else
    {
      // Set frame rate and Bayer method (only valid for non-Format7 modes)
      DoBayerConversion_ = findBayerMethod(newconfig.bayer_method.c_str());
      if (!Modes::setFrameRate(camera_, videoMode_, newconfig.frame_rate))
        {
          SafeCleanup();
          CAM_EXCEPT(bumblebee1394::Exception, "Failed to set frame rate");
          return -1;
        }
    }

  findBayerPattern(newconfig.bayer_pattern.c_str());

  use_ros_time_ = newconfig.use_ros_time;

  //////////////////////////////////////////////////////////////
  // start the device streaming data
  //////////////////////////////////////////////////////////////

  // Set camera to use DMA, improves performance.
  if (DC1394_SUCCESS != dc1394_capture_setup(camera_, NUM_DMA_BUFFERS,
                                             DC1394_CAPTURE_FLAGS_DEFAULT))
    {
      SafeCleanup();
      CAM_EXCEPT(bumblebee1394::Exception, "Failed to open device!");
      return -1;
    }
  
  // Start transmitting camera data
  if (DC1394_SUCCESS != dc1394_video_set_transmission(camera_, DC1394_ON))
    {
      SafeCleanup();
      CAM_EXCEPT(bumblebee1394::Exception, "Failed to start device!");
      return -1;
    }

  //////////////////////////////////////////////////////////////
  // initialize feature settings
  //////////////////////////////////////////////////////////////

  // TODO: pass newconfig here and eliminate initialize() method
  features_.reset(new Features(camera_));
 
  return 0;
}


/** Safe Cleanup -- may get called more than once. */
void Bumblebee1394::SafeCleanup()
{
  if (camera_)
    {
      format7_.stop();
      dc1394_capture_stop(camera_);
      dc1394_camera_free(camera_);
      camera_ = NULL;
    }
}


/** close the 1394 device */
int Bumblebee1394::close()
{
  if (camera_)
    {
      if (DC1394_SUCCESS != dc1394_video_set_transmission(camera_, DC1394_OFF)
          || DC1394_SUCCESS != dc1394_capture_stop(camera_))
        ROS_WARN("unable to stop camera");
    }

  // Free resources
  SafeCleanup();

  return 0;
}

std::string bayer_string(dc1394color_filter_t pattern, unsigned int bits)
{
  if (bits == 8)
    {
      switch (pattern)
        {
        case DC1394_COLOR_FILTER_RGGB:
          return sensor_msgs::image_encodings::BAYER_RGGB8;
        case DC1394_COLOR_FILTER_GBRG:
          return sensor_msgs::image_encodings::BAYER_GBRG8;
        case DC1394_COLOR_FILTER_GRBG:
          return sensor_msgs::image_encodings::BAYER_GRBG8;
        case DC1394_COLOR_FILTER_BGGR:
          return sensor_msgs::image_encodings::BAYER_BGGR8;
        default:
          return sensor_msgs::image_encodings::MONO8;
        }
    }
  else if (bits == 16)
    {
      switch (pattern)
        {
        case DC1394_COLOR_FILTER_RGGB:
          return sensor_msgs::image_encodings::BAYER_RGGB16;
        case DC1394_COLOR_FILTER_GBRG:
          return sensor_msgs::image_encodings::BAYER_GBRG16;
        case DC1394_COLOR_FILTER_GRBG:
          return sensor_msgs::image_encodings::BAYER_GRBG16;
        case DC1394_COLOR_FILTER_BGGR:
          return sensor_msgs::image_encodings::BAYER_BGGR16;
        default:
          return sensor_msgs::image_encodings::MONO16;
        }
    }

  // bits should always be 8 or 16, but if not MONO8 is a good default
  return sensor_msgs::image_encodings::MONO8;
}

/** Return an image frame */
void Bumblebee1394::readData(sensor_msgs::Image& image, sensor_msgs::Image& right_image, sensor_msgs::Image& center_image)
{
  ROS_ASSERT_MSG(camera_, "Attempt to read from camera that is not open.");

  dc1394video_frame_t * frame = NULL;
  dc1394_capture_dequeue (camera_, DC1394_CAPTURE_POLICY_WAIT, &frame);
  if (!frame)
    {
      CAM_EXCEPT(bumblebee1394::Exception, "Unable to capture frame");
      return;
    }
  
  uint8_t* capture_buffer;

  if (use_ros_time_)
    image.header.stamp = ros::Time::now();
  else
    image.header.stamp = ros::Time((double) frame->timestamp / 1000000.0);

  dc1394video_frame_t frame2;

  if (DoBayerConversion_)
    {
	  if (multicam_ != "mono") {
		  CAM_EXCEPT(bumblebee1394::Exception,
				  	 "Bayer conversion is only available for monocular cameras");
		  return;
	  }
      // debayer frame into RGB8
      size_t frame2_size = (frame->size[0] * frame->size[1]
                            * 3 * sizeof(unsigned char));
      frame2.image = (unsigned char *) malloc(frame2_size);
      frame2.allocated_image_bytes = frame2_size;
      frame2.color_coding = DC1394_COLOR_CODING_RGB8;

      frame->color_filter = BayerPattern_;
      int err = dc1394_debayer_frames(frame, &frame2, BayerMethod_);
      if (err != DC1394_SUCCESS)
        {
          free(frame2.image);
          dc1394_capture_enqueue(camera_, frame);
          CAM_EXCEPT(bumblebee1394::Exception, "Could not convert/debayer frames");
          return;
        }

      capture_buffer = reinterpret_cast<uint8_t *>(frame2.image);

      image.width = frame2.size[0];
      image.height = frame2.size[1];
    }
  else
    {
      image.width = frame->size[0];
      image.height = frame->size[1];
      capture_buffer = reinterpret_cast<uint8_t *>(frame->image);
    }

  ROS_ASSERT(capture_buffer);   

  int image_size;  
  switch (videoMode_)
    {
    case DC1394_VIDEO_MODE_160x120_YUV444:
      image.step=image.width*3;
      image_size = image.height*image.step;
      image.encoding = sensor_msgs::image_encodings::RGB8;
      image.data.resize(image_size);
      yuv::uyv2rgb(reinterpret_cast<unsigned char *> (capture_buffer),
                   reinterpret_cast<unsigned char *> (&image.data[0]),
                   image.width * image.height);
      break;
    case DC1394_VIDEO_MODE_640x480_YUV411:
      image.step=image.width*3;
      image_size = image.height*image.step;
      image.encoding = sensor_msgs::image_encodings::RGB8;
      image.data.resize(image_size);
      yuv::uyyvyy2rgb(reinterpret_cast<unsigned char *> (capture_buffer),
                      reinterpret_cast<unsigned char *> (&image.data[0]),
                      image.width * image.height);
      break;
    case DC1394_VIDEO_MODE_320x240_YUV422:
    case DC1394_VIDEO_MODE_640x480_YUV422:
    case DC1394_VIDEO_MODE_800x600_YUV422:
    case DC1394_VIDEO_MODE_1024x768_YUV422:
    case DC1394_VIDEO_MODE_1280x960_YUV422:
    case DC1394_VIDEO_MODE_1600x1200_YUV422:
      image.step=image.width*3;
      image_size = image.height*image.step;
      image.encoding = sensor_msgs::image_encodings::RGB8;
      image.data.resize(image_size);
      yuv::uyvy2rgb(reinterpret_cast<unsigned char *> (capture_buffer),
                    reinterpret_cast<unsigned char *> (&image.data[0]),
                    image.width * image.height);
      break;
    case DC1394_VIDEO_MODE_640x480_RGB8:
    case DC1394_VIDEO_MODE_800x600_RGB8:
    case DC1394_VIDEO_MODE_1024x768_RGB8:
    case DC1394_VIDEO_MODE_1280x960_RGB8:
    case DC1394_VIDEO_MODE_1600x1200_RGB8:
      image.step=image.width*3;
      image_size = image.height*image.step;
      image.encoding = sensor_msgs::image_encodings::RGB8;
      image.data.resize(image_size);
      memcpy(&image.data[0], capture_buffer, image_size);
      break;
    case DC1394_VIDEO_MODE_640x480_MONO8:
    case DC1394_VIDEO_MODE_800x600_MONO8:
    case DC1394_VIDEO_MODE_1024x768_MONO8:
    case DC1394_VIDEO_MODE_1280x960_MONO8:
    case DC1394_VIDEO_MODE_1600x1200_MONO8:
      if (!DoBayerConversion_)
        {
          image.step=image.width;
          image_size = image.height*image.step;
          // set Bayer encoding in ROS Image message
          image.encoding = bayer_string(BayerPattern_, 8);
          image.data.resize(image_size);
          memcpy(&image.data[0], capture_buffer, image_size);
        }
      else
        {
          image.step=image.width*3;
          image_size = image.height*image.step;
          image.encoding = sensor_msgs::image_encodings::RGB8;
          image.data.resize(image_size);
          memcpy(&image.data[0], capture_buffer, image_size);
        } 
      break;
    case DC1394_VIDEO_MODE_640x480_MONO16:
    case DC1394_VIDEO_MODE_800x600_MONO16:
    case DC1394_VIDEO_MODE_1024x768_MONO16:
    case DC1394_VIDEO_MODE_1280x960_MONO16:
    case DC1394_VIDEO_MODE_1600x1200_MONO16:
      if (!DoBayerConversion_)
        {
    	  if (multicam_ == "stereo")
    	  {
    		  image.step = image.width;
    		  image.encoding = bayer_string(BayerPattern_, 8);
    		  right_image = image;

    		  image_size = image.height * image.step;
    		  image.data.resize(image_size);
    		  right_image.data.resize(image_size);
    		  deinterlace_stereo(image, right_image, capture_buffer);
    	  }
    	  
    	  else
    	  {
			  image.step=image.width*2;
			  image_size = image.height*image.step;
			  image.encoding = bayer_string(BayerPattern_, 16);
			  image.is_bigendian = true;
			  image.data.resize(image_size);
			  memcpy(&image.data[0], capture_buffer, image_size);
    	  }
        }
      else
        {
          // @todo test Bayer conversions for mono16
          image.step=image.width*3;
          image_size = image.height*image.step;
          image.encoding = sensor_msgs::image_encodings::RGB8;
          image.data.resize(image_size);
          memcpy(&image.data[0], capture_buffer, image_size);
        } 
      break;
    default:
      if (dc1394_is_video_mode_scalable(videoMode_))
        {
          format7_.unpackData(image, right_image, center_image, capture_buffer);
           }
      else
        {
          CAM_EXCEPT(bumblebee1394::Exception, "Unknown image mode");
          return;
        }
    }
  dc1394_capture_enqueue(camera_, frame);

  if (DoBayerConversion_) 
    free(capture_buffer);
}

/** Deinterlace a 16-bit-per-channel image, creating two 8-bit ones
 *
 * @param left Left image, must have size and buffer configured
 * @param right Right image, must have size and buffer configured
 * @param buf Interlaced image data
 */
void Bumblebee1394::deinterlace_stereo(sensor_msgs::Image& left, sensor_msgs::Image& right,
                             unsigned char *buf)
{
  unsigned char *stacked = (unsigned char*) malloc(left.step * left.height * 2);
  ROS_ASSERT_MSG(stacked, "Unable to allocate memory for deinterlacing.");

  if (DC1394_SUCCESS != dc1394_deinterlace_stereo(buf, stacked, left.width, left.height * 2))
    {
      free(stacked);
      CAM_EXCEPT(bumblebee1394::Exception, "Unable to deinterlace stereo frame.");
    }

  memcpy(&left.data[0],
         stacked,
         left.step * left.height);

  memcpy(&right.data[0],
         stacked + right.step * right.height,
         right.step * right.height);

  free(stacked);
}

void Bumblebee1394::extract_images_xb3(sensor_msgs::Image& left, sensor_msgs::Image& right, sensor_msgs::Image& center, dc1394color_filter_t bayerTile,
                             unsigned char *buf)
{
 
		unsigned int imageSize = left.width * 3 * left.height;
		
		unsigned int nBufferSize = imageSize;
		unsigned char* pucDeInterlaced = new unsigned char[ nBufferSize ]; 
		unsigned char* pucRGB = new unsigned char[ 3 * nBufferSize ];
		unsigned char* pucGreen = new unsigned char[ nBufferSize ];
		unsigned char* pucLeft = NULL;
		unsigned char* pucCenter = NULL;
		unsigned char* pucRight = NULL;
		dc1394bayer_method_t bayerMethod = DC1394_BAYER_METHOD_HQLINEAR;


	// Deinterlace the grab buffer
	dc1394_deinterlace_rgb( buf,
				pucDeInterlaced,
				left.width,
				3*left.height );
	// Extract color from the bayer tile image. Note: this will alias colors on the top and bottom rows
	dc1394_bayer_decoding_8bit( pucDeInterlaced,
				pucRGB,
		 	 	left.width,
				3*left.height,
				bayerTile,
				bayerMethod );
	// Deinterlace the RGB Buffer
	/*dc1394_deinterlace_green( pucRGB,
				pucGreen,
				left.width,
				9*left.height );*/
	
	pucRight 	= pucRGB + 0 * (imageSize);
	pucCenter 	= pucRGB + 1 * (imageSize);
	pucLeft 	= pucRGB + 2 * (imageSize);

		memcpy(&left.data[0], pucLeft, imageSize);
		memcpy(&center.data[0], pucCenter, imageSize);
		memcpy(&right.data[0], pucRight, imageSize);

		// Free memory
		free(pucDeInterlaced);
		free(pucRGB);
		free(pucGreen);

}

void Bumblebee1394::dc1394_deinterlace_rgb( unsigned char* src, unsigned char* dest, unsigned int width, unsigned int height)
{
  register int i = (width*height)-1;
  register int r = ((width*height)/3)-1;
  register int g = ((width*height)*2/3)-1;
  register int b = (width*height)-1;

  while (i >= 0) {
    dest[r--] = src[i--];
    dest[g--] = src[i--];
    dest[b--] = src[i--];
  }
}

void Bumblebee1394::dc1394_deinterlace_green( unsigned char* src, unsigned char* dest, unsigned int width, unsigned int height)
 {
   register int i = (width*height)-2;
   register int g = ((width*height)/3)-1;
 
   while (i >= 0) {
     dest[g--] = src[i-=3];
   }
   
 }
