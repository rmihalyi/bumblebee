#include <ros/ros.h>
#include <nodelet/nodelet.h>
#include <image_transport/image_transport.h>
#include <image_transport/subscriber_filter.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/exact_time.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <camera_info_manager/camera_info_manager.h>

#include <image_geometry/stereo_camera_model.h>
#include <opencv2/calib3d/calib3d.hpp>

#include <sensor_msgs/image_encodings.h>
#include <stereo_msgs/DisparityImage.h>

#include <opencv_image_proc/DisparityClaheConfig.h>
#include <dynamic_reconfigure/server.h>

#include "opencv_image_proc/clahe.h"

#include <sensor_msgs/fill_image.h>


namespace opencv_image_proc {

using namespace sensor_msgs;
using namespace stereo_msgs;
using namespace message_filters::sync_policies;
using namespace cv;

class DisparityClaheNodelet : public nodelet::Nodelet
{
  boost::shared_ptr<image_transport::ImageTransport> it_;
  
  // Subscriptions
  image_transport::SubscriberFilter sub_l_image_, sub_r_image_;
  
  sensor_msgs::Image m_leftImage;
  sensor_msgs::Image m_rightImage;
  
  message_filters::Subscriber<CameraInfo> sub_l_info_, sub_r_info_;
  typedef ExactTime<Image, CameraInfo, Image, CameraInfo> ExactPolicy;
  typedef ApproximateTime<Image, CameraInfo, Image, CameraInfo> ApproximatePolicy;
  typedef message_filters::Synchronizer<ExactPolicy> ExactSync;
  typedef message_filters::Synchronizer<ApproximatePolicy> ApproximateSync;
  boost::shared_ptr<ExactSync> exact_sync_;
  boost::shared_ptr<ApproximateSync> approximate_sync_;

  void straightCLAHE(InputArray src, OutputArray dst);

  // Publications
  boost::mutex connect_mutex_;
  ros::Publisher pub_disparity_;
  ros::Publisher pub_right_eq, pub_left_eq;

  // Dynamic reconfigure
  boost::recursive_mutex config_mutex_;
  typedef opencv_image_proc::DisparityClaheConfig Config;
  typedef dynamic_reconfigure::Server<Config> ReconfigureServer;
  boost::shared_ptr<ReconfigureServer> reconfigure_server_;
  
  // Processing state (note: only safe because we're single-threaded!)
  image_geometry::StereoCameraModel model_;
  StereoBM block_matcher_; // contains scratch buffers for block matching

  virtual void onInit();

  void connectCb();

  void imageCb(const ImageConstPtr& l_image_msg, const CameraInfoConstPtr& l_info_msg,
               const ImageConstPtr& r_image_msg, const CameraInfoConstPtr& r_info_msg);

  void configCb(Config &config, uint32_t level);

  int xdivs;
  int ydivs;
  int bins;
  int limit_counter;

};

void DisparityClaheNodelet::onInit()
{
  ros::NodeHandle &nh = getNodeHandle();
  ros::NodeHandle &private_nh = getPrivateNodeHandle();

  it_.reset(new image_transport::ImageTransport(nh));

  // Synchronize inputs. Topic subscriptions happen on demand in the connection
  // callback. Optionally do approximate synchronization.
  int queue_size;
  private_nh.param("queue_size", queue_size, 5);
  bool approx;
  private_nh.param("approximate_sync", approx, false);
  if (approx)
  {
    approximate_sync_.reset( new ApproximateSync(ApproximatePolicy(queue_size),
                                                 sub_l_image_, sub_l_info_,
                                                 sub_r_image_, sub_r_info_) );
    approximate_sync_->registerCallback(boost::bind(&DisparityClaheNodelet::imageCb,
                                                    this, _1, _2, _3, _4));
  }
  else
  {
    exact_sync_.reset( new ExactSync(ExactPolicy(queue_size),
                                     sub_l_image_, sub_l_info_,
                                     sub_r_image_, sub_r_info_) );
    exact_sync_->registerCallback(boost::bind(&DisparityClaheNodelet::imageCb,
                                              this, _1, _2, _3, _4));
  }

  // Set up dynamic reconfiguration
  ReconfigureServer::CallbackType f = boost::bind(&DisparityClaheNodelet::configCb,
                                                  this, _1, _2);
  reconfigure_server_.reset(new ReconfigureServer(config_mutex_, private_nh));
  reconfigure_server_->setCallback(f);

  // Monitor whether anyone is subscribed to the output
  ros::SubscriberStatusCallback connect_cb = boost::bind(&DisparityClaheNodelet::connectCb, this);
  // Make sure we don't enter connectCb() between advertising and assigning to pub_disparity_
  boost::lock_guard<boost::mutex> lock(connect_mutex_);
  pub_disparity_ = nh.advertise<DisparityImage>("disparity_clahe", 1, connect_cb, connect_cb);
  
  pub_right_eq = nh.advertise<sensor_msgs::Image>("right_eq", 1);
  pub_left_eq = nh.advertise<sensor_msgs::Image>("left_eq", 1);
}

// Handles (un)subscribing when clients (un)subscribe
void DisparityClaheNodelet::connectCb()
{
  boost::lock_guard<boost::mutex> lock(connect_mutex_);
  if (pub_disparity_.getNumSubscribers() == 0)
  {
    sub_l_image_.unsubscribe();
    sub_l_info_ .unsubscribe();
    sub_r_image_.unsubscribe();
    sub_r_info_ .unsubscribe();
  }
  else if (!sub_l_image_.getSubscriber())
  {
    ros::NodeHandle &nh = getNodeHandle();
    // Queue size 1 should be OK; the one that matters is the synchronizer queue size.
    /// @todo Allow remapping left, right?
    image_transport::TransportHints hints("raw", ros::TransportHints(), getPrivateNodeHandle());
    sub_l_image_.subscribe(*it_, "left/image_rect", 1, hints);
    sub_l_info_ .subscribe(nh,   "left/camera_info", 1);
    sub_r_image_.subscribe(*it_, "right/image_rect", 1, hints);
    sub_r_info_ .subscribe(nh,   "right/camera_info", 1);
  }
}

void DisparityClaheNodelet::imageCb(const ImageConstPtr& l_image_msg,
                               const CameraInfoConstPtr& l_info_msg,
                               const ImageConstPtr& r_image_msg,
                               const CameraInfoConstPtr& r_info_msg)
{
  /// @todo Convert (share) with new cv_bridge
  assert(l_image_msg->encoding == sensor_msgs::image_encodings::MONO8);
  assert(r_image_msg->encoding == sensor_msgs::image_encodings::MONO8);

  // Update the camera model
  model_.fromCameraInfo(l_info_msg, r_info_msg);
  
  // Allocate new disparity image message
  DisparityImagePtr disp_msg = boost::make_shared<DisparityImage>();
  
  disp_msg->header         = l_info_msg->header;
  disp_msg->image.header   = l_info_msg->header;
  disp_msg->image.height   = l_image_msg->height;
  disp_msg->image.width    = l_image_msg->width;
  disp_msg->image.encoding = sensor_msgs::image_encodings::TYPE_32FC1;
  disp_msg->image.step     = disp_msg->image.width * sizeof(float);
  disp_msg->image.data.resize(disp_msg->image.height * disp_msg->image.step);

  // Stereo parameters
  disp_msg->f = model_.right().fx();
  disp_msg->T = model_.baseline();

  // Compute window of (potentially) valid disparities
  Ptr<CvStereoBMState> params = block_matcher_.state;
  int border   = params->SADWindowSize / 2;
  int left   = params->numberOfDisparities + params->minDisparity + border - 1;
  int wtf = (params->minDisparity >= 0) ? border + params->minDisparity : std::max(border, -params->minDisparity);
  int right  = disp_msg->image.width - 1 - wtf;
  int top    = border;
  int bottom = disp_msg->image.height - 1 - border;
  disp_msg->valid_window.x_offset = left;
  disp_msg->valid_window.y_offset = top;
  disp_msg->valid_window.width    = right - left;
  disp_msg->valid_window.height   = bottom - top;

  // Disparity search range
  disp_msg->min_disparity = params->minDisparity;
  disp_msg->max_disparity = params->minDisparity + params->numberOfDisparities - 1;
  disp_msg->delta_d = 1.0 / 16; // OpenCV uses 16 disparities per pixel

  // Create cv::Mat views onto all buffers
  const Mat_<uint8_t> l_image(l_image_msg->height, l_image_msg->width,
                                  const_cast<uint8_t*>(&l_image_msg->data[0]),
                                  l_image_msg->step);
  const Mat_<uint8_t> r_image(r_image_msg->height, r_image_msg->width,
                                  const_cast<uint8_t*>(&r_image_msg->data[0]),
                                  r_image_msg->step);
  Mat_<float> disp_image(disp_msg->image.height, disp_msg->image.width,
                             reinterpret_cast<float*>(&disp_msg->image.data[0]),
                             disp_msg->image.step);

  cv::Mat eq_left, eq_right;
  
  //straightCLAHE(l_image, eq_left);
  //straightCLAHE(r_image, eq_right);

  cv::equalizeHist( l_image, eq_left );
  cv::equalizeHist( r_image, eq_right );
   
  // Perform block matching to find the disparities
  //block_matcher_(eq_left, eq_right, disp_image, CV_32F);
  block_matcher_(eq_left, eq_right, disp_image, CV_32F);


  // Adjust for any x-offset between the principal points: d' = d - (cx_l - cx_r)
  double cx_l = model_.left().cx();
  double cx_r = model_.right().cx();
  if (cx_l != cx_r)
    subtract(disp_image, Scalar(cx_l - cx_r), disp_image);

  sensor_msgs::fillImage(m_leftImage, sensor_msgs::image_encodings::MONO8, eq_left.rows, eq_left.cols, eq_left.cols, eq_left.data);
  sensor_msgs::fillImage(m_rightImage, sensor_msgs::image_encodings::MONO8, eq_right.rows, eq_right.cols, eq_right.cols, eq_right.data);

  pub_left_eq.publish(m_leftImage);
  pub_right_eq.publish(m_rightImage);

  pub_disparity_.publish(disp_msg);
}

void DisparityClaheNodelet::straightCLAHE( InputArray _src, OutputArray _dst) {
             
  
    Mat src = _src.getMat();
    _dst.create( src.size(), src.type() );
    Mat dst = _dst.getMat();

    CvMat _csrc = src, _cdst = dst;

    cvCLAdaptEqualize(&_csrc, &_cdst, (unsigned int) xdivs, (unsigned int) ydivs,
                                        (unsigned int) bins,(float) 0.1 * limit_counter, CV_CLAHE_RANGE_INPUT);
    

    //cvAdaptEqualize(&_csrc, &_cdst, (unsigned int) xdivs, (unsigned int) ydivs,
    //                                    (unsigned int) bins, CV_CLAHE_RANGE_FULL);
      
   // _dst = Mat(_cdst);
}


void DisparityClaheNodelet::configCb(Config &config, uint32_t level)
{
  // Tweak all settings to be valid
  config.prefilter_size |= 0x1; // must be odd
  config.correlation_window_size |= 0x1; // must be odd
  config.disparity_range = (config.disparity_range / 16) * 16; // must be multiple of 16

  // Note: With single-threaded NodeHandle, configCb and imageCb can't be called
  // concurrently, so this is thread-safe.
  block_matcher_.state->preFilterSize       = config.prefilter_size;
  block_matcher_.state->preFilterCap        = config.prefilter_cap;
  block_matcher_.state->SADWindowSize       = config.correlation_window_size;
  block_matcher_.state->minDisparity        = config.min_disparity;
  block_matcher_.state->numberOfDisparities = config.disparity_range;
  block_matcher_.state->uniquenessRatio     = config.uniqueness_ratio;
  block_matcher_.state->textureThreshold    = config.texture_threshold;
  block_matcher_.state->speckleWindowSize   = config.speckle_size;
  block_matcher_.state->speckleRange        = config.speckle_range;

  xdivs = config.x_cells;
  ydivs = config.y_cells;
  bins = config.bins;
  limit_counter = config.limit_counter;

}

} // namespace stereo_image_proc

// Register nodelet
#include <pluginlib/class_list_macros.h>
PLUGINLIB_DECLARE_CLASS(opencv_image_proc, disparity_clahe,
                        opencv_image_proc::DisparityClaheNodelet, nodelet::Nodelet)
