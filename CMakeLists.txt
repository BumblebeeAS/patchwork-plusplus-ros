if($ENV{ROS_VERSION} STREQUAL "1")
  option(BUILD_JSK_PKGS "Enable building of required components of jsk_recognition_msgs and jsk_rviz_plugins" ON)
else()
  option(BUILD_JSK_PKGS "Enable building of required components of jsk_recognition_msgs and jsk_rviz_plugins" OFF)
endif()

if(BUILD_JSK_PKGS)
    add_subdirectory(include/jsk_recognition_msgs)
#     add_subdirectory(include/jsk_rviz_plugins) #TODO: allow building of rviz plugins as well
endif()

cmake_minimum_required(VERSION 3.5)
project(patchworkpp)

add_compile_options(-std=c++17)
set(CMAKE_BUILD_TYPE "Release")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if($ENV{ROS_VERSION} STREQUAL "1")
  add_definitions(-DTARGET_ROS1)

find_package(catkin REQUIRED COMPONENTS
        roscpp
        rospy
        std_msgs
        roslaunch
        cv_bridge
        pcl_conversions
        pcl_ros
        geometry_msgs
        laser_geometry
        sensor_msgs
        message_generation
        jsk_recognition_msgs
)
else()
  add_definitions(-DTARGET_ROS2)

  find_package(ament_cmake REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rclpy REQUIRED)
  find_package(pcl_ros REQUIRED)
  find_package(pcl_conversions REQUIRED)
  find_package(geometry_msgs REQUIRED)
  find_package(std_msgs REQUIRED)
  find_package(rosidl_default_generators REQUIRED)
  # find_package(roslaunch REQUIRED)
  find_package(sensor_msgs REQUIRED)
  # find_package(jsk_recognition_msgs REQUIRED)
  find_package(laser_geometry REQUIRED)
  find_package(cv_bridge REQUIRED)

  set(INCLUDE_DIRS include ${ament_cmake_INCLUDE_DIRS} ${rclcpp_INCLUDE_DIRS}
    ${rclpy_INCLUDE_DIRS} ${pcl_ros_INCLUDE_DIRS} ${pcl_conversions_INCLUDE_DIRS}
    ${geometry_msgs_INCLUDE_DIRS} ${std_msgs_INCLUDE_DIRS}
    ${rosidl_default_generators_INCLUDE_DIRS} 
    # ${roslaunch_INCLUDE_DIRS}
    ${sensor_msgs_INCLUDE_DIRS}
    #  ${jsk_recognition_msgs_INCLUDE_DIRS}
    ${laser_geometry_INCLUDE_DIRS} ${cv_bridge_INCLUDE_DIRS})
  include_directories(${INCLUDE_DIRS})

  set(LIBRARY_DIRS ${ament_cmake_LIBRARY_DIRS} ${rclcpp_LIBRARY_DIRS}
    ${rclpy_LIBRARY_DIRS} ${pcl_ros_LIBRARY_DIRS} ${pcl_conversions_LIBRARY_DIRS}
    ${geometry_msgs_LIBRARY_DIRS} ${std_msgs_LIBRARY_DIRS}
    ${rosidl_default_generators_LIBRARY_DIRS} 
    # ${roslaunch_LIBRARY_DIRS}
    ${sensor_msgs_LIBRARY_DIRS}
    #  ${jsk_recognition_msgs_LIBRARY_DIRS}
    ${laser_geometry_LIBRARY_DIRS} ${cv_bridge_LIBRARY_DIRS})

  link_directories(${LIBRARY_DIRS})

  set(LIBS ${ament_cmake_LIBRARIES} ${rclcpp_LIBRARIES} ${rclpy_LIBRARIES}
    ${pcl_ros_LIBRARIES} ${pcl_conversions_LIBRARIES} ${geometry_msgs_LIBRARIES}
    ${std_msgs_LIBRARIES} ${rosidl_default_generators_LIBRARIES}
    # ${roslaunch_LIBRARIES} 
    ${sensor_msgs_LIBRARIES}
    ${jsk_recognition_msgs_LIBRARIES} ${laser_geometry_LIBRARIES}
    ${cv_bridge_LIBRARIES})
endif()
add_compile_options(-std=c++17)
set(CMAKE_BUILD_TYPE "Release")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

find_package(PCL 1.7 REQUIRED)
find_package(Boost 1.54 REQUIRED)
find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

if($ENV{ROS_VERSION} STREQUAL "1")
  catkin_package(
          INCLUDE_DIRS
          LIBRARIES
          CATKIN_DEPENDS roscpp rospy std_msgs
  )

  include_directories(
          ${catkin_INCLUDE_DIRS}
          ${PCL_INCLUDE_DIRS}
          ${OpenCV_INCLUDE_DIRS}
          include
  )

  add_executable(offline_kitti src/offline_kitti.cpp)
  target_link_libraries(offline_kitti ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${OpenCV_LIBRARIES})
  add_dependencies(offline_kitti patchworkpp_generate_messages_cpp)

  add_executable(demo src/demo.cpp)
  target_link_libraries(demo ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${OpenCV_LIBRARIES})
  add_dependencies(demo patchworkpp_generate_messages_cpp)
  
  add_executable(video src/video.cpp)
  target_link_libraries(video ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${OpenCV_LIBRARIES})
  add_dependencies(video patchworkpp_generate_messages_cpp)
  
  # ==== Install ====
  install(TARGETS demo
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
  )

  install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})


else()
  add_executable(demo src/demo.cpp)
  target_link_libraries(demo ${LIBS} ${PCL_LIBRARIES} ${OpenCV_LIBRARIES})
  # ==== Install ====
  install(TARGETS demo
    ARCHIVE
    DESTINATION lib
    LIBRARY
    DESTINATION lib
    RUNTIME
    DESTINATION lib/${PROJECT_NAME})

  install(DIRECTORY include/${PROJECT_NAME}/
    DESTINATION include/${PROJECT_NAME})

  install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

  ament_export_dependencies(ament_cmake)
  ament_export_dependencies(rclcpp)
  ament_export_dependencies(rclpy)
  ament_export_dependencies(pcl_ros)
  ament_export_dependencies(pcl_conversions)
  ament_export_dependencies(geometry_msgs)
  ament_export_dependencies(std_msgs)
  ament_export_dependencies(rosidl_default_generators)
  # ament_export_dependencies(roslaunch)
  ament_export_dependencies(sensor_msgs)
  # ament_export_dependencies(jsk_recognition_msgs)
  ament_export_dependencies(laser_geometry)
  ament_export_dependencies(cv_bridge)
  ament_export_include_directories(${INCLUDE_DIRS})

  ament_package()
  endif()
