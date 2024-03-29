/* -*- mode: C++ -*- */
/* $Id: driver1394.h 36902 2011-05-26 23:20:18Z joq $ */

/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2010 Jack O'Quin
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

#include <boost/thread/mutex.hpp>

#include <ros/ros.h>
#include <camera_info_manager/camera_info_manager.h>
#include <driver_base/driver.h>
#include <dynamic_reconfigure/server.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/CameraInfo.h>

#include "dev_bumblebee1394.h"
#include "bumblebee1394/Bumblebee1394Config.h"
typedef bumblebee1394::Bumblebee1394Config Config;

/** @file

    @brief ROS driver interface for IIDC-compatible IEEE 1394 digital cameras.

*/

namespace bumblebee1394_driver
{

class Bumblebee1394Driver
{
public:

  // public methods
  Bumblebee1394Driver(ros::NodeHandle priv_nh,
                   ros::NodeHandle camera_nh);
  ~Bumblebee1394Driver();
  void poll(void);
  void setup(void);
  void shutdown(void);

private:

  // private methods
  void closeCamera();
  bool openCamera(Config &newconfig);
  void publish(const sensor_msgs::ImagePtr &image,
		  	   const sensor_msgs::ImagePtr &right_image, 
		  	   const sensor_msgs::ImagePtr &center_image);
  bool read(sensor_msgs::ImagePtr &left_image,
		  	sensor_msgs::ImagePtr &right_image, sensor_msgs::ImagePtr &center_image);
  void reconfig(bumblebee1394::Bumblebee1394Config &newconfig, uint32_t level);

  void checkCameraInfo(const sensor_msgs::ImagePtr& image,
		  	  	  	   sensor_msgs::CameraInfoPtr& info,
		  	  	  	   bool& calibration_matches,
		  	  	  	   std::string camera_name);

  /** Non-recursive mutex for serializing callbacks with device polling. */
  boost::mutex mutex_;

  volatile driver_base::Driver::state_t state_; // current driver state
  volatile bool reconfiguring_;        // true when reconfig() running

  ros::NodeHandle priv_nh_;             // private node handle
  ros::NodeHandle camera_nh_;           // camera name space handle
  ros::NodeHandle left_camera_nh_;
  ros::NodeHandle right_camera_nh_;
  ros::NodeHandle center_camera_nh_;
  std::string camera_name_;             // camera name

  /** libdc1394 camera device interface */
  boost::shared_ptr<bumblebee1394::Bumblebee1394> dev_;

  /** dynamic parameter configuration */
  bumblebee1394::Bumblebee1394Config config_;
  dynamic_reconfigure::Server<bumblebee1394::Bumblebee1394Config> srv_;
  ros::Rate cycle_;                     // polling rate when closed

  /** camera calibration information */
  boost::shared_ptr<camera_info_manager::CameraInfoManager> left_cinfo_;
  boost::shared_ptr<camera_info_manager::CameraInfoManager> right_cinfo_;
  boost::shared_ptr<camera_info_manager::CameraInfoManager> center_cinfo_;
  bool left_calibration_matches_;
  bool right_calibration_matches_;
  bool center_calibration_matches_;

  /** image transport interfaces */
  boost::shared_ptr<image_transport::ImageTransport> left_it_;
  boost::shared_ptr<image_transport::ImageTransport> right_it_;
  boost::shared_ptr<image_transport::ImageTransport> center_it_;
  image_transport::CameraPublisher left_image_pub_;
  image_transport::CameraPublisher right_image_pub_;
  image_transport::CameraPublisher center_image_pub_;  
  ros::Time m_startTime;
  int m_totalFrames;
  bool m_first;
  double m_frameRate;
  double m_infoPublishRate;

}; // end class Camera1394Driver

}; // end namespace camera1394_driver
