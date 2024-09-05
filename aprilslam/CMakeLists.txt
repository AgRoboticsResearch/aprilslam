cmake_minimum_required(VERSION 2.8.3)
project(aprilslam)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_FLAGS "-std=c++11 -Ofast -Wall")

find_package(catkin REQUIRED COMPONENTS
    roscpp 
    image_transport 
    cv_bridge 
    image_geometry
    std_msgs 
    tf2_ros 
    rosbag
    pcl_conversions
    pcl_ros
    )

find_package(cmake_modules)
find_package(GTSAM REQUIRED QUIET)
find_package(Eigen3 REQUIRED QUIET)
find_package(yaml-cpp REQUIRED QUIET)

add_message_files(DIRECTORY msg FILES Apriltag.msg Apriltags.msg)
generate_messages(DEPENDENCIES std_msgs geometry_msgs)

catkin_package()
find_path(TBB_INCLUDE_DIR tbb/tbb.h PATHS /usr/include)
find_library(TBB_LIBRARY tbb PATHS /usr/lib/x86_64-linux-gnu)
find_library(TBB_MALLOC_LIBRARY tbbmalloc PATHS /usr/lib/x86_64-linux-gnu)

# 检查 TBB 库是否存在
if (TBB_INCLUDE_DIR AND TBB_LIBRARY AND TBB_MALLOC_LIBRARY)
  message(STATUS "Found Intel TBB")
  include_directories(${TBB_INCLUDE_DIR})
  set(TBB_LIBRARIES ${TBB_LIBRARY} ${TBB_MALLOC_LIBRARY})
else()
  message(FATAL_ERROR "Intel TBB not found")
endif()

# 创建 TBB::tbb 和 TBB::tbbmalloc 目标
if(NOT TARGET TBB::tbb)
  add_library(TBB::tbb SHARED IMPORTED)
  set_target_properties(TBB::tbb PROPERTIES
    IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libtbb.so"
    INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
  )
endif()

if(NOT TARGET TBB::tbbmalloc)
  add_library(TBB::tbbmalloc SHARED IMPORTED)
  set_target_properties(TBB::tbbmalloc PROPERTIES
    IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libtbbmalloc.so"
    INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
  )
endif()

include_directories(
    include
    ${catkin_INCLUDE_DIRS}
    ${GTSAM_INCLUDE_DIRS}
    ${Eigen_INCLUDE_DIRS}
    )

# aprilslam library
add_library(${PROJECT_NAME}
    src/mapper/mapper.cpp
    src/mapper/tag_map.cpp
    src/utils/utils.cpp
    src/visualizer/visualizer.cpp
    src/mapper/mapper_node.cpp
    )
target_link_libraries(${PROJECT_NAME}
    ${catkin_LIBRARIES}
    gtsam
    yaml-cpp
    )

add_executable(mapper_node
    src/mapper/mapper_main.cpp
    )
target_link_libraries(mapper_node
    ${PROJECT_NAME}
    gtsam
    )

add_executable(zed_camera_info_pub 
src/utils/zed_camera_info_pub.cpp
)

target_link_libraries(zed_camera_info_pub
${PROJECT_NAME})

add_executable(bagtime_converter 
src/utils/bagtime_converter.cpp
)

target_link_libraries(bagtime_converter
${PROJECT_NAME})

add_executable(zed_image_splitter 
src/utils/zed_image_splitter.cpp
)

target_link_libraries(zed_image_splitter
${PROJECT_NAME})

add_executable(pcd_rviz_viewer 
src/utils/pcd_rviz_viewer.cpp
)

target_link_libraries(pcd_rviz_viewer
${PROJECT_NAME})

add_executable(tf_publisher 
src/utils/tf_publisher.cpp
)

target_link_libraries(tf_publisher
${PROJECT_NAME})

# dependencies
add_dependencies(${PROJECT_NAME}
    ${catkin_EXPORTED_TARGETS}
    ${PROJECT_NAME}_gencfg
    ${PROJECT_NAME}_gencpp
    ${${PROJECT_NAME}_EXPORTED_TARGETS}
    )
