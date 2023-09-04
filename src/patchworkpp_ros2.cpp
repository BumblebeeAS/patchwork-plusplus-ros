#include <iostream>
// For disable PCL complile lib, to use PointXYZIR
#define PCL_NO_PRECOMPILE


#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include "patchworkpp/patchworkpp_ros2.hpp"
#include <signal.h>
#include <pcl_conversions/pcl_conversions.h>


using PointType = pcl::PointXYZI;
using namespace std;

namespace patchworkpp {
    using PatchworkppPointXYZI = PatchWorkpp<pcl::PointXYZI>;
}

RCLCPP_COMPONENTS_REGISTER_NODE(patchworkpp::PatchworkppPointXYZI)

// std::shared_ptr<PatchWorkpp<PointType>> PatchworkppGroundSeg;

// int main(int argc, char**argv) {
//     rclcpp::init(argc, argv);
//     rclcpp::NodeOptions options;
//     // ros::NodeHandle nh;
//     // ros::NodeHandle pnh("~");


//     cout << "Operating patchwork++..." << endl;
//     PatchworkppGroundSeg.reset(new PatchWorkpp<PointType>(options));
//     rclcpp::spin(PatchworkppGroundSeg);
//     rclcpp::shutdown();

//     return 0;
// }
