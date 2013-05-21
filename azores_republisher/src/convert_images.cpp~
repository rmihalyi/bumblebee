#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <nodelet/nodelet.h>
#include <dynamic_reconfigure/server.h>
#include <camera_info_manager/camera_info_manager.h>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
//#include <azores_republisher/ConvertImagesConfig.h>


namespace azores_republisher {

class ConvertImagesNodelet : public nodelet::Nodelet
{
  ros::NodeHandle privNH_; 
  // Subscriptions
  boost::shared_ptr<image_transport::ImageTransport> it_;
  image_transport::Subscriber sub_l_image_, sub_r_image_, sub_c_image_ ;

  // Publications
  boost::mutex connect_mutex_;
  image_transport::CameraPublisher pub_right_, pub_left_, pub_center_;
  
  boost::shared_ptr<camera_info_manager::CameraInfoManager> cleft_info_;
  boost::shared_ptr<camera_info_manager::CameraInfoManager> cright_info_;
  boost::shared_ptr<camera_info_manager::CameraInfoManager> ccenter_info_;
  
  virtual void onInit();
  void connectCb();
  void rightImageCb(const sensor_msgs::ImageConstPtr& raw_msg);
  void leftImageCb(const sensor_msgs::ImageConstPtr& raw_msg);
  void centerImageCb(const sensor_msgs::ImageConstPtr& raw_msg);
};

void ConvertImagesNodelet::onInit()
{

  ros::NodeHandle &nh = getNodeHandle();
  ros::NodeHandle &private_nh = getPrivateNodeHandle();
  
  it_.reset(new image_transport::ImageTransport(nh));

  std::string cal_left_file;
  std::string cal_right_file;
  std::string cal_center_file;
  std::string camera_name;

  private_nh.param("camera", camera_name, std::string("camera"));
  
  private_nh.param("left_camera_info_url", cal_left_file, std::string(""));
  private_nh.param("right_camera_info_url", cal_right_file, std::string(""));
  private_nh.param("center_camera_info_url", cal_center_file, std::string(""));

  
  cleft_info_ = boost::shared_ptr<camera_info_manager::CameraInfoManager>(new camera_info_manager::CameraInfoManager(nh, camera_name, cal_left_file));
  cright_info_= boost::shared_ptr<camera_info_manager::CameraInfoManager>(new camera_info_manager::CameraInfoManager(nh, camera_name, cal_right_file));
  ccenter_info_= boost::shared_ptr<camera_info_manager::CameraInfoManager>(new camera_info_manager::CameraInfoManager(nh, camera_name, cal_center_file));

  if (cleft_info_->validateURL(cal_left_file))
  {
	cleft_info_->loadCameraInfo(cal_left_file);
    ROS_WARN("left calibration file is loaded");
  } 
  else
  {
	 ROS_WARN("Cannot load left calibration file");
 }
 
  if (cright_info_->validateURL(cal_right_file))
  {
	cright_info_->loadCameraInfo(cal_right_file);
    ROS_WARN("right calibration file is loaded");
  }
 else
 {
	 ROS_WARN("Cannot load right calibration file");
 }
 
   if (ccenter_info_->validateURL(cal_center_file))
  {
	ccenter_info_->loadCameraInfo(cal_center_file);
    ROS_WARN("center calibration file is loaded");
  } 
  else
  {
	 ROS_WARN("Cannot load center calibration file");
 }
 
  // Monitor whether anyone is subscribed to the output
  image_transport::SubscriberStatusCallback connect_cb = boost::bind(&ConvertImagesNodelet::connectCb, this);

  // Make sure we don't enter connectCb() between advertising and assigning 
  boost::lock_guard<boost::mutex> lock(connect_mutex_);

  pub_left_ = it_->advertiseCamera("/stereo/left/image_raw", 1, connect_cb, connect_cb);
  pub_right_ = it_->advertiseCamera("/stereo/right/image_raw", 1, connect_cb, connect_cb);
  pub_center_ = it_->advertiseCamera("/stereo/center/image_raw", 1, connect_cb, connect_cb);

}

// Handles (un)subscribing when clients (un)subscribe
void ConvertImagesNodelet::connectCb()
{
  boost::lock_guard<boost::mutex> lock(connect_mutex_);
  if (pub_left_.getNumSubscribers() == 0)
  {
    sub_l_image_.shutdown();
  }
  else if (!sub_l_image_)
  {
    image_transport::TransportHints hints("raw", ros::TransportHints(), getPrivateNodeHandle());
    sub_l_image_ = it_->subscribe("/left/image_raw", 1, &ConvertImagesNodelet::leftImageCb, this, hints);
  }
  
  if (pub_right_.getNumSubscribers() == 0)
  {
    sub_r_image_.shutdown();
  }
  else if (!sub_r_image_)
  {
    image_transport::TransportHints hints("raw", ros::TransportHints(), getPrivateNodeHandle());
    sub_r_image_ = it_->subscribe("/right/image_raw", 1, &ConvertImagesNodelet::rightImageCb, this, hints);
  }

  
  if (pub_center_.getNumSubscribers() == 0)
  {
    sub_c_image_.shutdown();
  }  
  else if (!sub_c_image_)
  {
    image_transport::TransportHints hints("raw", ros::TransportHints(), getPrivateNodeHandle());
    sub_c_image_ = it_->subscribe("/center/image_raw", 1, &ConvertImagesNodelet::centerImageCb, this, hints);
  }
 
}

void ConvertImagesNodelet::rightImageCb(const sensor_msgs::ImageConstPtr& raw_msg)
{
	sensor_msgs::CameraInfoPtr cr(new sensor_msgs::CameraInfo(cright_info_->getCameraInfo()));
	cr->header.frame_id = "camera";
	cr->header.stamp = raw_msg->header.stamp;
	pub_right_.publish(*raw_msg, *cr);
}

void ConvertImagesNodelet::centerImageCb(const sensor_msgs::ImageConstPtr& raw_msg)
{
	sensor_msgs::CameraInfoPtr cc(new sensor_msgs::CameraInfo(ccenter_info_->getCameraInfo()));
	cc->header.frame_id = "camera";
	cc->header.stamp = raw_msg->header.stamp;
	pub_center_.publish(*raw_msg, *cc);

}

void ConvertImagesNodelet::leftImageCb(const sensor_msgs::ImageConstPtr& raw_msg)
{
	sensor_msgs::CameraInfoPtr cl(new sensor_msgs::CameraInfo(cleft_info_->getCameraInfo()));
	cl->header.frame_id = "camera";
	cl->header.stamp = raw_msg->header.stamp;
    pub_left_.publish(*raw_msg, *cl);
}

} // namespace camRep

// Register as nodelet
#include <pluginlib/class_list_macros.h>
PLUGINLIB_DECLARE_CLASS (azores_republisher, convert_images, azores_republisher::ConvertImagesNodelet, nodelet::Nodelet);
