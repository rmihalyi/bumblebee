# common source files for both node and nodelet driver interfaces
set(DRIVER_SOURCES 
  driver1394.cpp
  dev_bumblebee1394.cpp
  features.cpp
  format7.cpp
  modes.cpp)

rosbuild_add_library(bumblebee1394_nodelet nodelet.cpp ${DRIVER_SOURCES})

find_library(DC1394 dc1394)
target_link_libraries(bumblebee1394_nodelet ${DC1394})
