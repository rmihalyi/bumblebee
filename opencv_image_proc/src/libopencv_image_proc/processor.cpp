#include <ros/assert.h>
#include "opencv_image_proc/processor.h"
#include <sensor_msgs/image_encodings.h>
#include <cmath>
#include <limits>

namespace opencv_image_proc {

bool StereoProcessor::process(const sensor_msgs::ImageConstPtr& left_raw,
                              const sensor_msgs::ImageConstPtr& right_raw,
                              const image_geometry::StereoCameraModel& model,
                              StereoImageSet& output, int flags) const
{
  // Do monocular processing on left and right images
  int left_flags = flags & LEFT_ALL;
  int right_flags = flags & RIGHT_ALL;
  if (flags & STEREO_ALL) {
    // Need the rectified images for stereo processing
    left_flags |= LEFT_RECT;
    right_flags |= RIGHT_RECT;
  }
  if (flags & (POINT_CLOUD | POINT_CLOUD2)) {
    flags |= DISPARITY;
    // Need the color channels for the point cloud
    left_flags |= LEFT_RECT_COLOR;
  }
  if (!mono_processor_.process(left_raw, model.left(), output.left, left_flags))
    return false;
  if (!mono_processor_.process(right_raw, model.right(), output.right, right_flags >> 4))
    return false;

  // Do block matching to produce the disparity image
  if (flags & DISPARITY) {
    processDisparity(output.left.rect, output.right.rect, model, output.disparity);
  }

  // Project disparity image to 3d point cloud
  if (flags & POINT_CLOUD) {
    processPoints(output.disparity, output.left.rect_color, output.left.color_encoding, model, output.points);
  }

  // Project disparity image to 3d point cloud
  if (flags & POINT_CLOUD2) {
    processPoints2(output.disparity, output.left.rect_color, output.left.color_encoding, model, output.points2);
  }

  return true;
}

void StereoProcessor::processDisparity(const cv::Mat& left_rect, const cv::Mat& right_rect,
                                       const image_geometry::StereoCameraModel& model,
                                       stereo_msgs::DisparityImage& disparity) const
{
  // Fixed-point disparity is 16 times the true value: d = d_fp / 16.0 = x_l - x_r.
  static const int DPP = 16; // disparities per pixel
  static const double inv_dpp = 1.0 / DPP;

  // Block matcher produces 16-bit signed (fixed point) disparity image
  block_matcher_(left_rect, right_rect, disparity16_);

  // Fill in DisparityImage image data, converting to 32-bit float
  sensor_msgs::Image& dimage = disparity.image;
  dimage.height = disparity16_.rows;
  dimage.width = disparity16_.cols;
  dimage.encoding = sensor_msgs::image_encodings::TYPE_32FC1;
  dimage.step = dimage.width * sizeof(float);
  dimage.data.resize(dimage.step * dimage.height);
  cv::Mat_<float> dmat(dimage.height, dimage.width, (float*)&dimage.data[0], dimage.step);
  // We convert from fixed-point to float disparity and also adjust for any x-offset between
  // the principal points: d = d_fp*inv_dpp - (cx_l - cx_r)
  disparity16_.convertTo(dmat, dmat.type(), inv_dpp, -(model.left().cx() - model.right().cx()));
  ROS_ASSERT(dmat.data == &dimage.data[0]);
  /// @todo is_bigendian? :)

  // Stereo parameters
  disparity.f = model.right().fx();
  disparity.T = model.baseline();

  /// @todo Window of (potentially) valid disparities

  // Disparity search range
  disparity.min_disparity = getMinDisparity();
  disparity.max_disparity = getMinDisparity() + getDisparityRange() - 1;
  disparity.delta_d = inv_dpp;
}

inline bool isValidPoint(const cv::Vec3f& pt)
{
  // Check both for disparities explicitly marked as invalid (where OpenCV maps pt.z to MISSING_Z)
  // and zero disparities (point mapped to infinity).
  return pt[2] != image_geometry::StereoCameraModel::MISSING_Z && !std::isinf(pt[2]);
}

void StereoProcessor::processPoints(const stereo_msgs::DisparityImage& disparity,
                                    const cv::Mat& color, const std::string& encoding,
                                    const image_geometry::StereoCameraModel& model,
                                    sensor_msgs::PointCloud& points) const
{
  // Calculate dense point cloud
  const sensor_msgs::Image& dimage = disparity.image;
  const cv::Mat_<float> dmat(dimage.height, dimage.width, (float*)&dimage.data[0], dimage.step);
  model.projectDisparityImageTo3d(dmat, dense_points_, true);

  // Fill in sparse point cloud message
  points.points.resize(0);
  points.channels.resize(3);
  points.channels[0].name = "rgb";
  points.channels[0].values.resize(0);
  points.channels[1].name = "u";
  points.channels[1].values.resize(0);
  points.channels[2].name = "v";
  points.channels[2].values.resize(0);
  
  for (int32_t u = 0; u < dense_points_.rows; ++u) {
    for (int32_t v = 0; v < dense_points_.cols; ++v) {
      if (isValidPoint(dense_points_(u,v))) {
        // x,y,z
        geometry_msgs::Point32 pt;
        pt.x = dense_points_(u,v)[0];
        pt.y = dense_points_(u,v)[1];
        pt.z = dense_points_(u,v)[2];
        points.points.push_back(pt);
        // u,v
        points.channels[1].values.push_back(u);
        points.channels[2].values.push_back(v);
      }
    }
  }

  // Fill in color
  namespace enc = sensor_msgs::image_encodings;
  points.channels[0].values.reserve(points.points.size());
  if (encoding == enc::MONO8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v) {
        if (isValidPoint(dense_points_(u,v))) {
          uint8_t g = color.at<uint8_t>(u,v);
          int32_t rgb = (g << 16) | (g << 8) | g;
          points.channels[0].values.push_back(*(float*)(&rgb));
        }
      }
    }
  }
  else if (encoding == enc::RGB8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v) {
        if (isValidPoint(dense_points_(u,v))) {
          const cv::Vec3b& rgb = color.at<cv::Vec3b>(u,v);
          int32_t rgb_packed = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
          points.channels[0].values.push_back(*(float*)(&rgb_packed));
        }
      }
    }
  }
  else if (encoding == enc::BGR8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v) {
        if (isValidPoint(dense_points_(u,v))) {
          const cv::Vec3b& bgr = color.at<cv::Vec3b>(u,v);
          int32_t rgb_packed = (bgr[2] << 16) | (bgr[1] << 8) | bgr[0];
          points.channels[0].values.push_back(*(float*)(&rgb_packed));
        }
      }
    }
  }
  else {
    ROS_WARN("Could not fill color channel of the point cloud, unrecognized encoding '%s'", encoding.c_str());
  }
}

void StereoProcessor::processPoints2(const stereo_msgs::DisparityImage& disparity,
                                     const cv::Mat& color, const std::string& encoding,
                                     const image_geometry::StereoCameraModel& model,
                                     sensor_msgs::PointCloud2& points) const
{
  // Calculate dense point cloud
  const sensor_msgs::Image& dimage = disparity.image;
  const cv::Mat_<float> dmat(dimage.height, dimage.width, (float*)&dimage.data[0], dimage.step);
  model.projectDisparityImageTo3d(dmat, dense_points_, true);

  // Fill in sparse point cloud message
  points.height = dense_points_.rows;
  points.width  = dense_points_.cols;
  points.fields.resize (4);
  points.fields[0].name = "x";
  points.fields[0].offset = 0;
  points.fields[0].count = 1;
  points.fields[0].datatype = sensor_msgs::PointField::FLOAT32;
  points.fields[1].name = "y";
  points.fields[1].offset = 4;
  points.fields[1].count = 1;
  points.fields[1].datatype = sensor_msgs::PointField::FLOAT32;
  points.fields[2].name = "z";
  points.fields[2].offset = 8;
  points.fields[2].count = 1;
  points.fields[2].datatype = sensor_msgs::PointField::FLOAT32;
  points.fields[3].name = "rgb";
  points.fields[3].offset = 12;
  points.fields[3].count = 1;
  points.fields[3].datatype = sensor_msgs::PointField::FLOAT32;
  //points.is_bigendian = false; ???
  points.point_step = 16;
  points.row_step = points.point_step * points.width;
  points.data.resize (points.row_step * points.height);
  points.is_dense = false; // there may be invalid points
 
  float bad_point = std::numeric_limits<float>::quiet_NaN ();
  int i = 0;
  for (int32_t u = 0; u < dense_points_.rows; ++u) {
    for (int32_t v = 0; v < dense_points_.cols; ++v, ++i) {
      if (isValidPoint(dense_points_(u,v))) {
        // x,y,z,rgba
        memcpy (&points.data[i * points.point_step + 0], &dense_points_(u,v)[0], sizeof (float));
        memcpy (&points.data[i * points.point_step + 4], &dense_points_(u,v)[1], sizeof (float));
        memcpy (&points.data[i * points.point_step + 8], &dense_points_(u,v)[2], sizeof (float));
      }
      else {
        memcpy (&points.data[i * points.point_step + 0], &bad_point, sizeof (float));
        memcpy (&points.data[i * points.point_step + 4], &bad_point, sizeof (float));
        memcpy (&points.data[i * points.point_step + 8], &bad_point, sizeof (float));
      }
    }
  }

  // Fill in color
  namespace enc = sensor_msgs::image_encodings;
  i = 0;
  if (encoding == enc::MONO8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v, ++i) {
        if (isValidPoint(dense_points_(u,v))) {
          uint8_t g = color.at<uint8_t>(u,v);
          int32_t rgb = (g << 16) | (g << 8) | g;
          memcpy (&points.data[i * points.point_step + 12], &rgb, sizeof (int32_t));
        }
        else {
          memcpy (&points.data[i * points.point_step + 12], &bad_point, sizeof (float));
        }
      }
    }
  }
  else if (encoding == enc::RGB8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v, ++i) {
        if (isValidPoint(dense_points_(u,v))) {
          const cv::Vec3b& rgb = color.at<cv::Vec3b>(u,v);
          int32_t rgb_packed = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
          memcpy (&points.data[i * points.point_step + 12], &rgb_packed, sizeof (int32_t));
        }
        else {
          memcpy (&points.data[i * points.point_step + 12], &bad_point, sizeof (float));
        }
      }
    }
  }
  else if (encoding == enc::BGR8) {
    for (int32_t u = 0; u < dense_points_.rows; ++u) {
      for (int32_t v = 0; v < dense_points_.cols; ++v, ++i) {
        if (isValidPoint(dense_points_(u,v))) {
          const cv::Vec3b& bgr = color.at<cv::Vec3b>(u,v);
          int32_t rgb_packed = (bgr[2] << 16) | (bgr[1] << 8) | bgr[0];
          memcpy (&points.data[i * points.point_step + 12], &rgb_packed, sizeof (int32_t));
        }
        else {
          memcpy (&points.data[i * points.point_step + 12], &bad_point, sizeof (float));
        }
      }
    }
  }
  else {
    ROS_WARN("Could not fill color channel of the point cloud, unrecognized encoding '%s'", encoding.c_str());
  }
}

} //namespace stereo_image_proc
