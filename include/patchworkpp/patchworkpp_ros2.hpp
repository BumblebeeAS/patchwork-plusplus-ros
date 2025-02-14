/**
 * @file patchworkpp.hpp
 * @author Seungjae Lee
 * @brief
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef PATCHWORKPP_ROS2_H
#define PATCHWORKPP_ROS2_H

#include <sensor_msgs/msg/point_cloud2.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_value.hpp>
#include <eigen3/Eigen/Dense>

#include <boost/format.hpp>
#include <numeric>
#include <queue>
#include <mutex>

#include <patchworkpp/utils.hpp>
#include "rclcpp_components/register_node_macro.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"

#define MARKER_Z_VALUE -2.2
#define UPRIGHT_ENOUGH 0.55
#define FLAT_ENOUGH 0.2
#define TOO_HIGH_ELEVATION 0.0
#define TOO_TILTED 1.0

#define NUM_HEURISTIC_MAX_PTS_IN_PATCH 3000

using Eigen::JacobiSVD;
using Eigen::MatrixXf;
using Eigen::VectorXf;

using namespace std;

/*
    @brief PathWork ROS Node.
*/
template <typename PointT>
bool point_z_cmp(PointT a, PointT b)
{
  return a.z < b.z;
}

template <typename PointT>
struct RevertCandidate
{
  int concentric_idx;
  int sector_idx;
  double ground_flatness;
  double line_variable;
  Eigen::Vector4f pc_mean;
  pcl::PointCloud<PointT> regionwise_ground;

  RevertCandidate(
      int _c_idx, int _s_idx, double _flatness, double _line_var,
      Eigen::Vector4f _pc_mean, pcl::PointCloud<PointT> _ground) :
    concentric_idx(_c_idx),
    sector_idx(_s_idx),
    ground_flatness(_flatness),
    line_variable(_line_var),
    pc_mean(_pc_mean),
    regionwise_ground(_ground)
  {}
};

template <typename PointT>
class PatchWorkpp : public rclcpp::Node
{
public:
  typedef std::vector<pcl::PointCloud<PointT>> Ring;
  typedef std::vector<Ring> Zone;

  PatchWorkpp(const rclcpp::NodeOptions& options) : Node("patchworkpp", options)
  {
    // Init ROS related
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Inititalizing PatchWork++...");
    declare_parameter("verbose", rclcpp::PARAMETER_BOOL);
    declare_parameter("sensor_height", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("num_iter", rclcpp::PARAMETER_INTEGER);
    declare_parameter("num_lpr", rclcpp::PARAMETER_INTEGER);
    declare_parameter("num_min_pts", rclcpp::PARAMETER_INTEGER);
    declare_parameter("th_seeds", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("th_dist", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("th_seeds_v", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("th_dist_v", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("max_r", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("min_r", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("uprightness_thr", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("adaptive_seed_selection_margin", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("RNR_ver_angle_thr", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("RNR_intensity_thr", rclcpp::PARAMETER_DOUBLE);
    declare_parameter("max_flatness_storage", rclcpp::PARAMETER_INTEGER);
    declare_parameter("max_elevation_storage", rclcpp::PARAMETER_INTEGER);
    declare_parameter("enable_RNR", rclcpp::PARAMETER_BOOL);
    declare_parameter("enable_RVPF", rclcpp::PARAMETER_BOOL);
    declare_parameter("enable_TGR", rclcpp::PARAMETER_BOOL);

    declare_parameter("czm.num_zones", rclcpp::PARAMETER_INTEGER);

    this->get_node_parameters_interface()->declare_parameter(
        "czm.num_sectors_each_zone", rclcpp::ParameterValue(std::vector<int64_t>{}));
    this->get_node_parameters_interface()->declare_parameter(
        "czm.num_rings_each_zone", rclcpp::ParameterValue(std::vector<int64_t>{}));
    declare_parameter<std::vector<double>>("czm.elevation_thresholds");
    declare_parameter<std::vector<double>>("czm.flatness_thresholds");
    declare_parameter("visualize", rclcpp::PARAMETER_BOOL);

    verbose_ = get_parameter("verbose").as_bool();
    sensor_height_ = get_parameter("sensor_height").as_double();
    num_iter_ = get_parameter("num_iter").as_int();
    num_lpr_ = get_parameter("num_lpr").as_int();
    num_min_pts_ = get_parameter("num_min_pts").as_int();
    th_seeds_ = get_parameter("th_seeds").as_double();
    th_dist_ = get_parameter("th_dist").as_double();
    th_seeds_v_ = get_parameter("th_seeds_v").as_double();
    th_dist_v_ = get_parameter("th_dist_v").as_double();
    max_range_ = get_parameter("max_r").as_double();
    min_range_ = get_parameter("min_r").as_double();
    uprightness_thr_ = get_parameter("uprightness_thr").as_double();
    adaptive_seed_selection_margin_ =
        get_parameter("adaptive_seed_selection_margin").as_double();
    RNR_ver_angle_thr_ = get_parameter("RNR_ver_angle_thr").as_double();
    RNR_intensity_thr_ = get_parameter("RNR_intensity_thr").as_double();
    max_flatness_storage_ = get_parameter("max_flatness_storage").as_int();
    max_elevation_storage_ = get_parameter("max_elevation_storage").as_int();
    enable_RNR_ = get_parameter("enable_RNR").as_bool();
    enable_RVPF_ = get_parameter("enable_RVPF").as_bool();
    enable_TGR_ = get_parameter("enable_TGR").as_bool();

    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Sensor Height: %f", sensor_height_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Num of Iteration: %d", num_iter_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Num of LPR: %d", num_lpr_);
    RCLCPP_INFO(
        rclcpp::get_logger("patchworkpp"), "Num of min. points: %d", num_min_pts_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Seeds Threshold: %f", th_seeds_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Distance Threshold: %f", th_dist_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Max. range:: %f", max_range_);
    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "Min. range:: %f", min_range_);
    RCLCPP_INFO(
        rclcpp::get_logger("patchworkpp"), "Normal vector threshold: %f",
        uprightness_thr_);
    RCLCPP_INFO(
        rclcpp::get_logger("patchworkpp"), "adaptive_seed_selection_margin: %f",
        adaptive_seed_selection_margin_);
    RCLCPP_INFO(
        rclcpp::get_logger("patchworkpp"), "RNR_ver_angle_thr: %f", RNR_ver_angle_thr_);

    // CZM denotes 'Concentric Zone Model'. Please refer to our paper
    num_zones_ = get_parameter("czm.num_zones").as_int();
    num_sectors_each_zone_ =
        get_parameter("czm.num_sectors_each_zone").as_integer_array();
    num_rings_each_zone_ = get_parameter("czm.num_rings_each_zone").as_integer_array();
    elevation_thr_ = get_parameter("czm.elevation_thresholds").as_double_array();
    flatness_thr_ = get_parameter("czm.flatness_thresholds").as_double_array();

    RCLCPP_INFO(
        rclcpp::get_logger("patchworkpp"), "Num. zones: %d %ld %ld", num_zones_,
        num_sectors_each_zone_.size(), num_rings_each_zone_.size());

    if (num_zones_ != num_sectors_each_zone_.size() ||
        num_sectors_each_zone_.size() != num_rings_each_zone_.size())
    {
      throw invalid_argument("Some parameters are wrong! Check the num_zones and "
                             "num_rings/sectors_each_zone");
    }
    if (elevation_thr_.size() != flatness_thr_.size())
    {
      throw invalid_argument(
          "Some parameters are wrong! Check the elevation/flatness_thresholds");
    }

    num_rings_of_interest_ = elevation_thr_.size();

    visualize_ = get_parameter("visualize").as_bool();

    int num_polygons = std::inner_product(
        num_rings_each_zone_.begin(), num_rings_each_zone_.end(),
        num_sectors_each_zone_.begin(), 0);
    // poly_list_.header.frame_id = "map";
    // poly_list_.polygons.reserve(num_polygons);

    revert_pc_.reserve(NUM_HEURISTIC_MAX_PTS_IN_PATCH);
    ground_pc_.reserve(NUM_HEURISTIC_MAX_PTS_IN_PATCH);
    regionwise_ground_.reserve(NUM_HEURISTIC_MAX_PTS_IN_PATCH);
    regionwise_nonground_.reserve(NUM_HEURISTIC_MAX_PTS_IN_PATCH);

    // PlaneViz        = node_handle_.advertise<jsk_recognition_msgs::PolygonArray>("plane", 100, true);
    pub_revert_pc = this->create_publisher<sensor_msgs::msg::PointCloud2>("plane", 100);
    pub_reject_pc = this->create_publisher<sensor_msgs::msg::PointCloud2>("plane", 100);
    pub_normal = this->create_publisher<sensor_msgs::msg::PointCloud2>("plane", 100);
    pub_noise = this->create_publisher<sensor_msgs::msg::PointCloud2>("plane", 100);
    pub_vertical = this->create_publisher<sensor_msgs::msg::PointCloud2>("plane", 100);

    min_ranges_.resize(num_zones_);
    sector_sizes_.resize(num_zones_);
    ring_sizes_.resize(num_zones_);
    min_ranges_.push_back(min_range_);
    sector_sizes_.push_back(2 * M_PI / num_sectors_each_zone_.at(0));
    for (int i = 1; i < num_zones_; i++)
    {
      float scale = std::pow(2.0, num_zones_ - i);
      min_ranges_.push_back(((scale - 1.0) * min_range_ + max_range_) / scale);
      // RCLCPP_INFO(
      //     rclcpp::get_logger("patchworkpp"), "min_range %d %d %d", scale, i, min_ranges_.at(i));
      sector_sizes_.push_back(2 * M_PI / num_sectors_each_zone_.at(i));
      ring_sizes_.push_back(
          (min_ranges_.at(i) - min_ranges_.at(i - 1)) / num_rings_each_zone_.at(i - 1));
    }
    ring_sizes_.push_back(
        (max_range_ - min_ranges_.at(num_zones_ - 1)) /
        num_rings_each_zone_.at(num_zones_ - 1));
    // min_range_z2_ = (15 * min_range_ + max_range_) / 16.0;
    // min_range_z3_ = (7 * min_range_ + max_range_) / 8.0;
    // min_range_z4_ = (3 * min_range_ + max_range_) / 4.0;
    // min_range_z5_ = (min_range_ + max_range_) / 2.0;

    // min_ranges_ = {min_range_, min_range_z2_, min_range_z3_, min_range_z4_, min_range_z5_};
    // ring_sizes_ = {(min_range_z2_ - min_range_) / num_rings_each_zone_.at(0),
    //               (min_range_z3_ - min_range_z2_) / num_rings_each_zone_.at(1),
    //               (min_range_z4_ - min_range_z3_) / num_rings_each_zone_.at(2),
    //               (min_range_z5_ - min_range_z4_) / num_rings_each_zone_.at(3),
    //               (max_range_ - min_range_z5_) / num_rings_each_zone_.at(4)};
    // sector_sizes_ = {2 * M_PI / num_sectors_each_zone_.at(0),
    //                 2 * M_PI / num_sectors_each_zone_.at(1),
    //                 2 * M_PI / num_sectors_each_zone_.at(2),
    //                 2 * M_PI / num_sectors_each_zone_.at(3),
    //                 2 * M_PI / num_sectors_each_zone_.at(4)};

    RCLCPP_INFO(rclcpp::get_logger("patchworkpp"), "INITIALIZATION COMPLETE");

    for (int i = 0; i < num_zones_; i++)
    {
      Zone z;
      initialize_zone(z, num_sectors_each_zone_.at(i), num_rings_each_zone_.at(i));
      ConcentricZoneModel_.push_back(z);
    }

    declare_parameter("cloud_topic", "/pointcloud");
    cloud_topic = get_parameter("cloud_topic").as_string();
    pub_cloud = this->create_publisher<sensor_msgs::msg::PointCloud2>("cloud", 100);
    pub_ground = this->create_publisher<sensor_msgs::msg::PointCloud2>("ground", 100);
    pub_non_ground =
        this->create_publisher<sensor_msgs::msg::PointCloud2>("nonground", 100);
    sub_cloud = this->create_subscription<sensor_msgs::msg::PointCloud2>(
        cloud_topic, 1,
        std::bind(&PatchWorkpp<PointT>::callbackCloud, this, std::placeholders::_1));
    callback_handle_ = this->add_on_set_parameters_callback(
        std::bind(&PatchWorkpp<PointT>::parametersCallback, this, std::placeholders::_1));
  };

  void estimate_ground(
      pcl::PointCloud<PointT> cloud_in, pcl::PointCloud<PointT>& cloud_ground,
      pcl::PointCloud<PointT>& cloud_nonground, double& time_taken);

private:
  // Every private member variable is written with the undescore("_") in its end.

  // ros::NodeHandle node_handle_;

  std::recursive_mutex mutex_;

  int num_iter_;
  int num_lpr_;
  int num_min_pts_;
  int num_zones_;
  int num_rings_of_interest_;
  std::string cloud_topic;

  double sensor_height_;
  double th_seeds_;
  double th_dist_;
  double th_seeds_v_;
  double th_dist_v_;
  double max_range_;
  double min_range_;
  double uprightness_thr_;
  double adaptive_seed_selection_margin_;
  std::vector<double> min_ranges_;
  double min_range_z2_;   // 12.3625
  double min_range_z3_;   // 22.025
  double min_range_z4_;   // 41.35
  double min_range_z5_;   // 41.35
  double RNR_ver_angle_thr_;
  double RNR_intensity_thr_;

  bool verbose_;
  bool enable_RNR_;
  bool enable_RVPF_;
  bool enable_TGR_;

  int max_flatness_storage_, max_elevation_storage_;
  std::vector<double> update_flatness_[5];
  std::vector<double> update_elevation_[5];

  float d_;

  VectorXf normal_;
  MatrixXf pnormal_;
  VectorXf singular_values_;
  Eigen::Matrix3f cov_;
  Eigen::Vector4f pc_mean_;

  // For visualization
  bool visualize_;

  vector<long> num_sectors_each_zone_;
  vector<long> num_rings_each_zone_;

  vector<double> sector_sizes_;
  vector<double> ring_sizes_;
  vector<double> elevation_thr_;
  vector<double> flatness_thr_;

  queue<int> noise_idxs_;

  vector<Zone> ConcentricZoneModel_;

  // jsk_recognition_msgs::PolygonArray poly_list_;

  // ros::Publisher PlaneViz,
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub_revert_pc,
      pub_reject_pc, pub_normal, pub_noise, pub_vertical, pub_cloud, pub_ground,
      pub_non_ground;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_cloud;
  OnSetParametersCallbackHandle::SharedPtr callback_handle_;
  pcl::PointCloud<PointT> revert_pc_, reject_pc_, noise_pc_, vertical_pc_;
  pcl::PointCloud<PointT> ground_pc_;

  pcl::PointCloud<pcl::PointXYZINormal> normals_;

  pcl::PointCloud<PointT> regionwise_ground_, regionwise_nonground_;

  void initialize_zone(Zone& z, int num_sectors, int num_rings);

  void flush_patches_in_zone(Zone& patches, int num_sectors, int num_rings);
  void flush_patches(std::vector<Zone>& czm);

  void pc2czm(
      const pcl::PointCloud<PointT>& src, std::vector<Zone>& czm,
      pcl::PointCloud<PointT>& cloud_nonground);

  void reflected_noise_removal(
      pcl::PointCloud<PointT>& cloud, pcl::PointCloud<PointT>& cloud_nonground);

  void temporal_ground_revert(
      pcl::PointCloud<PointT>& cloud_ground, pcl::PointCloud<PointT>& cloud_nonground,
      std::vector<double> ring_flatness, std::vector<RevertCandidate<PointT>> candidates,
      int concentric_idx);

  void calc_mean_stdev(std::vector<double> vec, double& mean, double& stdev);

  void update_elevation_thr();
  void update_flatness_thr();

  double xy2theta(const double& x, const double& y);

  double xy2radius(const double& x, const double& y);

  void estimate_plane(const pcl::PointCloud<PointT>& ground);

  void extract_piecewiseground(
      const int zone_idx, const pcl::PointCloud<PointT>& src,
      pcl::PointCloud<PointT>& dst, pcl::PointCloud<PointT>& non_ground_dst);

  void extract_initial_seeds(
      const int zone_idx, const pcl::PointCloud<PointT>& p_sorted,
      pcl::PointCloud<PointT>& init_seeds);

  void extract_initial_seeds(
      const int zone_idx, const pcl::PointCloud<PointT>& p_sorted,
      pcl::PointCloud<PointT>& init_seeds, double th_seed);

  void callbackCloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr& cloud_msg);

  /* ROS Callbacks Functions */
  rcl_interfaces::msg::SetParametersResult
  parametersCallback(const std::vector<rclcpp::Parameter>& parameters);

  sensor_msgs::msg::PointCloud2 cloud2msg(
      pcl::PointCloud<PointT> cloud, const rclcpp::Time& stamp,
      std::string frame_id = "map");

  /***
     * For visulization of Ground Likelihood Estimation
     */
  // geometry_msgs::PolygonStamped set_polygons(int zone_idx, int r_idx, int theta_idx, int num_split);

  // void set_ground_likelihood_estimation_status(
  //         const int zone_idx, const int ring_idx,
  //         const int concentric_idx,
  //         const double z_vec,
  //         const double z_elevation,
  //         const double ground_flatness);
};

template <typename PointT>
inline void PatchWorkpp<PointT>::initialize_zone(Zone& z, int num_sectors, int num_rings)
{
  z.clear();
  pcl::PointCloud<PointT> cloud;
  cloud.reserve(1000);
  Ring ring;
  for (int i = 0; i < num_sectors; i++)
  {
    ring.emplace_back(cloud);
  }
  for (int j = 0; j < num_rings; j++)
  {
    z.emplace_back(ring);
  }
}

template <typename PointT>
inline void
PatchWorkpp<PointT>::flush_patches_in_zone(Zone& patches, int num_sectors, int num_rings)
{
  for (int i = 0; i < num_sectors; i++)
  {
    for (int j = 0; j < num_rings; j++)
    {
      if (!patches[j][i].points.empty())
        patches[j][i].points.clear();
    }
  }
}

template <typename PointT>
inline void PatchWorkpp<PointT>::flush_patches(vector<Zone>& czm)
{
  for (int k = 0; k < num_zones_; k++)
  {
    for (int i = 0; i < num_rings_each_zone_[k]; i++)
    {
      for (int j = 0; j < num_sectors_each_zone_[k]; j++)
      {
        if (!czm[k][i][j].points.empty())
          czm[k][i][j].points.clear();
      }
    }
  }

  if (verbose_)
    cout << "Flushed patches" << endl;
}

template <typename PointT>
inline void PatchWorkpp<PointT>::estimate_plane(const pcl::PointCloud<PointT>& ground)
{
  pcl::computeMeanAndCovarianceMatrix(ground, cov_, pc_mean_);
  // Singular Value Decomposition: SVD
  Eigen::JacobiSVD<Eigen::MatrixXf> svd(cov_, Eigen::DecompositionOptions::ComputeFullU);
  singular_values_ = svd.singularValues();

  // use the least singular vector as normal
  normal_ = (svd.matrixU().col(2));

  if (normal_(2) < 0)
  {
    for (int i = 0; i < 3; i++)
      normal_(i) *= -1;
  }

  // mean ground seeds value
  Eigen::Vector3f seeds_mean = pc_mean_.head<3>();

  // according to normal.T*[x,y,z] = -d
  d_ = -(normal_.transpose() * seeds_mean)(0, 0);
}

template <typename PointT>
inline void PatchWorkpp<PointT>::extract_initial_seeds(
    const int zone_idx, const pcl::PointCloud<PointT>& p_sorted,
    pcl::PointCloud<PointT>& init_seeds, double th_seed)
{
  init_seeds.points.clear();

  // LPR is the mean of low point representative
  double sum = 0;
  int cnt = 0;

  int init_idx = 0;
  if (zone_idx == 0)
  {
    for (int i = 0; i < p_sorted.points.size(); i++)
    {
      if (p_sorted.points[i].z < adaptive_seed_selection_margin_ * sensor_height_)
      {
        ++init_idx;
      }
      else
      {
        break;
      }
    }
  }

  // Calculate the mean height value.
  for (int i = init_idx; i < p_sorted.points.size() && cnt < num_lpr_; i++)
  {
    sum += p_sorted.points[i].z;
    cnt++;
  }
  double lpr_height = cnt != 0 ? sum / cnt : 0;   // in case divide by 0

  // iterate pointcloud, filter those height is less than lpr.height+th_seeds_
  for (int i = 0; i < p_sorted.points.size(); i++)
  {
    if (p_sorted.points[i].z < lpr_height + th_seed)
    {
      init_seeds.points.push_back(p_sorted.points[i]);
    }
  }
}

template <typename PointT>
inline void PatchWorkpp<PointT>::extract_initial_seeds(
    const int zone_idx, const pcl::PointCloud<PointT>& p_sorted,
    pcl::PointCloud<PointT>& init_seeds)
{
  init_seeds.points.clear();

  // LPR is the mean of low point representative
  double sum = 0;
  int cnt = 0;

  int init_idx = 0;
  if (zone_idx == 0)
  {
    for (int i = 0; i < p_sorted.points.size(); i++)
    {
      if (p_sorted.points[i].z < adaptive_seed_selection_margin_ * sensor_height_)
      {
        ++init_idx;
      }
      else
      {
        break;
      }
    }
  }

  // Calculate the mean height value.
  for (int i = init_idx; i < p_sorted.points.size() && cnt < num_lpr_; i++)
  {
    sum += p_sorted.points[i].z;
    cnt++;
  }
  double lpr_height = cnt != 0 ? sum / cnt : 0;   // in case divide by 0

  // iterate pointcloud, filter those height is less than lpr.height+th_seeds_
  for (int i = 0; i < p_sorted.points.size(); i++)
  {
    if (p_sorted.points[i].z < lpr_height + th_seeds_)
    {
      init_seeds.points.push_back(p_sorted.points[i]);
    }
  }
}

template <typename PointT>
inline void PatchWorkpp<PointT>::reflected_noise_removal(
    pcl::PointCloud<PointT>& cloud_in, pcl::PointCloud<PointT>& cloud_nonground)
{
  for (int i = 0; i < cloud_in.size(); i++)
  {
    double r = sqrt(cloud_in[i].x * cloud_in[i].x + cloud_in[i].y * cloud_in[i].y);
    double z = cloud_in[i].z;
    double ver_angle_in_deg = atan2(z, r) * 180 / M_PI;

    if (ver_angle_in_deg < RNR_ver_angle_thr_ && z < -sensor_height_ - 0.8)
    {
      if constexpr (std::is_same_v<pcl::PointXYZI, PointT>)
      {
        if (cloud_in[i].intensity >= RNR_intensity_thr_)
        {
          continue;
        }
      }
      cloud_nonground.push_back(cloud_in[i]);
      noise_pc_.push_back(cloud_in[i]);
      noise_idxs_.push(i);
    }
  }

  if (verbose_)
    cout << "[ RNR ] Num of noises : " << noise_pc_.points.size() << endl;
}

template <typename PointT>
inline rcl_interfaces::msg::SetParametersResult
PatchWorkpp<PointT>::parametersCallback(const std::vector<rclcpp::Parameter>& parameters)
{
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  result.reason = "success";
  verbose_ = get_parameter("verbose").as_bool();
  sensor_height_ = get_parameter("sensor_height").as_double();
  num_iter_ = get_parameter("num_iter").as_int();
  num_lpr_ = get_parameter("num_lpr").as_int();
  num_min_pts_ = get_parameter("num_min_pts").as_int();
  th_seeds_ = get_parameter("th_seeds").as_double();
  th_dist_ = get_parameter("th_dist").as_double();
  th_seeds_v_ = get_parameter("th_seeds_v").as_double();
  th_dist_v_ = get_parameter("th_dist_v").as_double();
  max_range_ = get_parameter("max_r").as_double();
  min_range_ = get_parameter("min_r").as_double();
  uprightness_thr_ = get_parameter("uprightness_thr").as_double();
  adaptive_seed_selection_margin_ =
      get_parameter("adaptive_seed_selection_margin").as_double();
  RNR_ver_angle_thr_ = get_parameter("RNR_ver_angle_thr").as_double();
  RNR_intensity_thr_ = get_parameter("RNR_intensity_thr").as_double();
  max_flatness_storage_ = get_parameter("max_flatness_storage").as_int();
  max_elevation_storage_ = get_parameter("max_elevation_storage").as_int();
  enable_RNR_ = get_parameter("enable_RNR").as_bool();
  enable_RVPF_ = get_parameter("enable_RVPF").as_bool();
  enable_TGR_ = get_parameter("enable_TGR").as_bool();
  auto new_num_zones_ = get_parameter("czm.num_zones").as_int();
  auto new_num_sectors_each_zone_ =
      get_parameter("czm.num_sectors_each_zone").as_integer_array();
  auto new_num_rings_each_zone_ =
      get_parameter("czm.num_rings_each_zone").as_integer_array();
  auto new_elevation_thr_ = get_parameter("czm.elevation_thresholds").as_double_array();
  auto new_flatness_thr_ = get_parameter("czm.flatness_thresholds").as_double_array();
  auto new_cloud_topic = get_parameter("cloud_topic").as_string();
  if (new_cloud_topic != cloud_topic)
  {
    cloud_topic = new_cloud_topic;
    sub_cloud = Node::create_subscription<sensor_msgs::msg::PointCloud2>(
        cloud_topic, 100,
        std::bind(&PatchWorkpp<PointT>::callbackCloud, this, std::placeholders::_1));
  }

  if (new_num_zones_ != new_num_sectors_each_zone_.size() ||
      new_num_sectors_each_zone_.size() != new_num_rings_each_zone_.size())
  {
    result.successful = false;
    std::cout << new_num_sectors_each_zone_.size() << new_num_rings_each_zone_.size()
              << std::endl;
    result.reason =
        "Some parameters are wrong! Check the num_zones and num_rings/sectors_each_zone";
  }
  else
  {
    num_zones_ = new_num_zones_;
    num_sectors_each_zone_ = new_num_rings_each_zone_;
    num_rings_each_zone_ = new_num_rings_each_zone_;
  }
  if (new_elevation_thr_.size() != new_flatness_thr_.size())
  {
    result.successful = false;
    result.reason = "Some parameters are wrong! Check the elevation/flatness_thresholds";
  }
  else
  {
    elevation_thr_ = new_elevation_thr_;
    flatness_thr_ = new_flatness_thr_;
  }

  return result;
}

/*
    @brief Velodyne pointcloud callback function. The main GPF pipeline is here.
    PointCloud SensorMsg -> Pointcloud -> z-value sorted Pointcloud
    ->error points removal -> extract ground seeds -> ground plane fit mainloop
*/

template <typename PointT>
inline void PatchWorkpp<PointT>::estimate_ground(
    pcl::PointCloud<PointT> cloud_in, pcl::PointCloud<PointT>& cloud_ground,
    pcl::PointCloud<PointT>& cloud_nonground, double& time_taken)
{
  unique_lock<recursive_mutex> lock(mutex_);

  // poly_list_.header.stamp = ros::Time::now();
  // poly_list_.header.frame_id = cloud_in.header.frame_id;
  // if (!poly_list_.polygons.empty()) poly_list_.polygons.clear();
  // if (!poly_list_.likelihood.empty()) poly_list_.likelihood.clear();

  static double start, t0, t1, t2, end;

  double pca_time_ = 0.0;
  double t_revert = 0.0;
  double t_total_ground = 0.0;
  double t_total_estimate = 0.0;

  // start = ros::Time::now().toSec();
  start = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

  cloud_ground.clear();
  cloud_nonground.clear();

  // 1. Reflected Noise Removal (RNR)
  if (enable_RNR_)
    reflected_noise_removal(cloud_in, cloud_nonground);

  t1 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

  // 2. Concentric Zone Model (CZM)
  flush_patches(ConcentricZoneModel_);
  pc2czm(cloud_in, ConcentricZoneModel_, cloud_nonground);

  t2 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

  int concentric_idx = 0;

  double t_sort = 0;

  std::vector<RevertCandidate<PointT>> candidates;
  std::vector<double> ringwise_flatness;

  for (int zone_idx = 0; zone_idx < num_zones_; ++zone_idx)
  {
    auto zone = ConcentricZoneModel_[zone_idx];

    for (int ring_idx = 0; ring_idx < num_rings_each_zone_[zone_idx]; ++ring_idx)
    {
      for (int sector_idx = 0; sector_idx < num_sectors_each_zone_[zone_idx];
           ++sector_idx)
      {
        if (zone[ring_idx][sector_idx].points.size() < num_min_pts_)
        {
          cloud_nonground += zone[ring_idx][sector_idx];
          continue;
        }

        // --------- region-wise sorting (faster than global sorting method) ---------------- //
        double t_sort_0 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

        sort(
            zone[ring_idx][sector_idx].points.begin(),
            zone[ring_idx][sector_idx].points.end(), point_z_cmp<PointT>);

        double t_sort_1 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();
        t_sort += (t_sort_1 - t_sort_0);
        // ---------------------------------------------------------------------------------- //

        double t_tmp0 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();
        extract_piecewiseground(
            zone_idx, zone[ring_idx][sector_idx], regionwise_ground_,
            regionwise_nonground_);

        double t_tmp1 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();
        t_total_ground += t_tmp1 - t_tmp0;
        pca_time_ += t_tmp1 - t_tmp0;

        // Status of each patch
        // used in checking uprightness, elevation, and flatness, respectively
        const double ground_uprightness = normal_(2);
        const double ground_elevation = pc_mean_(2, 0);
        const double ground_flatness = singular_values_.minCoeff();
        const double line_variable = singular_values_(1) != 0 ?
                                         singular_values_(0) / singular_values_(1) :
                                         std::numeric_limits<double>::max();

        double heading = 0.0;
        for (int i = 0; i < 3; i++)
          heading += pc_mean_(i, 0) * normal_(i);

        // if (visualize_) {
        //     auto polygons = set_polygons(zone_idx, ring_idx, sector_idx, 3);
        //     polygons.header = poly_list_.header;
        //     poly_list_.polygons.push_back(polygons);
        //     set_ground_likelihood_estimation_status(zone_idx, ring_idx, concentric_idx, ground_uprightness, ground_elevation, ground_flatness);

        //     pcl::PointXYZINormal tmp_p;
        //     tmp_p.x = pc_mean_(0,0);
        //     tmp_p.y = pc_mean_(1,0);
        //     tmp_p.z = pc_mean_(2,0);
        //     tmp_p.normal_x = normal_(0);
        //     tmp_p.normal_y = normal_(1);
        //     tmp_p.normal_z = normal_(2);
        //     normals_.points.emplace_back(tmp_p);
        // }

        double t_tmp2 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

        /*
                    About 'is_heading_outside' condidition, heading should be smaller than 0 theoretically.
                    ( Imagine the geometric relationship between the surface normal vector on the ground plane and
                        the vector connecting the sensor origin and the mean point of the ground plane )

                    However, when the patch is far awaw from the sensor origin,
                    heading could be larger than 0 even if it's ground due to lack of amount of ground plane points.

                    Therefore, we only check this value when concentric_idx < num_rings_of_interest ( near condition )
                */
        bool is_upright = ground_uprightness > uprightness_thr_;
        bool is_not_elevated = ground_elevation < elevation_thr_[concentric_idx];
        bool is_flat = ground_flatness < flatness_thr_[concentric_idx];
        bool is_near_zone = concentric_idx < num_rings_of_interest_;
        bool is_heading_outside = heading < 0.0;

        /*
                    Store the elevation & flatness variables
                    for A-GLE (Adaptive Ground Likelihood Estimation)
                    and TGR (Temporal Ground Revert). More information in the paper Patchwork++.
                */
        if (is_upright && is_not_elevated && is_near_zone)
        {
          update_elevation_[concentric_idx].push_back(ground_elevation);
          update_flatness_[concentric_idx].push_back(ground_flatness);

          ringwise_flatness.push_back(ground_flatness);
        }

        // Ground estimation based on conditions
        if (!is_upright)
        {
          cloud_nonground += regionwise_ground_;
        }
        else if (!is_near_zone)
        {
          cloud_ground += regionwise_ground_;
        }
        else if (!is_heading_outside)
        {
          cloud_nonground += regionwise_ground_;
        }
        else if (is_not_elevated || is_flat)
        {
          cloud_ground += regionwise_ground_;
        }
        else
        {
          RevertCandidate<PointT> candidate(
              concentric_idx, sector_idx, ground_flatness, line_variable, pc_mean_,
              regionwise_ground_);
          candidates.push_back(candidate);
        }
        // Every regionwise_nonground is considered nonground.
        cloud_nonground += regionwise_nonground_;

        double t_tmp3 = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();
        t_total_estimate += t_tmp3 - t_tmp2;
      }

      double t_bef_revert = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

      if (!candidates.empty())
      {
        if (enable_TGR_)
        {
          temporal_ground_revert(
              cloud_ground, cloud_nonground, ringwise_flatness, candidates,
              concentric_idx);
        }
        else
        {
          for (size_t i = 0; i < candidates.size(); i++)
          {
            cloud_nonground += candidates[i].regionwise_ground;
          }
        }

        candidates.clear();
        ringwise_flatness.clear();
      }

      double t_aft_revert = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

      t_revert += t_aft_revert - t_bef_revert;

      concentric_idx++;
    }
  }

  double t_update = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();

  update_elevation_thr();
  update_flatness_thr();

  end = rclcpp::Clock{RCL_STEADY_TIME}.now().seconds();
  time_taken = end - start;

  // cout << "Time taken : " << time_taken << endl;
  // cout << "Time taken to sort: " << t_sort << endl;
  // cout << "Time taken to pca : " << pca_time_ << endl;
  // cout << "Time taken to estimate: " << t_total_estimate << endl;
  // cout << "Time taken to Revert: " <<  t_revert << endl;
  // cout << "Time taken to update : " << end - t_update << endl;

  if (visualize_)
  {
    sensor_msgs::msg::PointCloud2 cloud_ROS;
    pcl::toROSMsg(revert_pc_, cloud_ROS);
    cloud_ROS.header.stamp = rclcpp::Clock{RCL_STEADY_TIME}.now();
    cloud_ROS.header.frame_id = cloud_in.header.frame_id;
    pub_revert_pc->publish(cloud_ROS);

    pcl::toROSMsg(reject_pc_, cloud_ROS);
    cloud_ROS.header.stamp = rclcpp::Clock{RCL_STEADY_TIME}.now();
    cloud_ROS.header.frame_id = cloud_in.header.frame_id;
    pub_reject_pc->publish(cloud_ROS);

    pcl::toROSMsg(normals_, cloud_ROS);
    cloud_ROS.header.stamp = rclcpp::Clock{RCL_STEADY_TIME}.now();
    cloud_ROS.header.frame_id = cloud_in.header.frame_id;
    pub_normal->publish(cloud_ROS);

    pcl::toROSMsg(noise_pc_, cloud_ROS);
    cloud_ROS.header.stamp = rclcpp::Clock{RCL_STEADY_TIME}.now();
    cloud_ROS.header.frame_id = cloud_in.header.frame_id;
    pub_noise->publish(cloud_ROS);

    pcl::toROSMsg(vertical_pc_, cloud_ROS);
    cloud_ROS.header.stamp = rclcpp::Clock{RCL_STEADY_TIME}.now();
    cloud_ROS.header.frame_id = cloud_in.header.frame_id;
    pub_vertical->publish(cloud_ROS);
  }

  // if(visualize_)
  // {
  //     PlaneViz->publish(poly_list_);
  // }

  revert_pc_.clear();
  reject_pc_.clear();
  normals_.clear();
  noise_pc_.clear();
  vertical_pc_.clear();
}

template <typename PointT>
inline void PatchWorkpp<PointT>::update_elevation_thr(void)
{
  for (int i = 0; i < num_rings_of_interest_; i++)
  {
    if (update_elevation_[i].empty())
      continue;

    double update_mean = 0.0, update_stdev = 0.0;
    calc_mean_stdev(update_elevation_[i], update_mean, update_stdev);
    if (i == 0)
    {
      elevation_thr_[i] = update_mean + 3 * update_stdev;
      sensor_height_ = -update_mean;
    }
    else
      elevation_thr_[i] = update_mean + 2 * update_stdev;

    // if (verbose_) cout << "elevation threshold [" << i << "]: " << elevation_thr_[i] << endl;

    int exceed_num = update_elevation_[i].size() - max_elevation_storage_;
    if (exceed_num > 0)
      update_elevation_[i].erase(
          update_elevation_[i].begin(), update_elevation_[i].begin() + exceed_num);
  }

  if (verbose_)
  {
    cout << "sensor height: " << sensor_height_ << endl;
    cout << (boost::format("elevation_thr_  :   %0.4f,  %0.4f,  %0.4f,  %0.4f") %
             elevation_thr_[0] % elevation_thr_[1] % elevation_thr_[2] %
             elevation_thr_[3])
                .str()
         << endl;
  }

  return;
}

template <typename PointT>
inline void PatchWorkpp<PointT>::update_flatness_thr(void)
{
  for (int i = 0; i < num_rings_of_interest_; i++)
  {
    if (update_flatness_[i].empty())
      break;
    if (update_flatness_[i].size() <= 1)
      break;

    double update_mean = 0.0, update_stdev = 0.0;
    calc_mean_stdev(update_flatness_[i], update_mean, update_stdev);
    flatness_thr_[i] = update_mean + update_stdev;

    // if (verbose_) { cout << "flatness threshold [" << i << "]: " << flatness_thr_[i] << endl; }

    int exceed_num = update_flatness_[i].size() - max_flatness_storage_;
    if (exceed_num > 0)
      update_flatness_[i].erase(
          update_flatness_[i].begin(), update_flatness_[i].begin() + exceed_num);
  }

  if (verbose_)
  {
    cout << (boost::format("flatness_thr_   :   %0.4f,  %0.4f,  %0.4f,  %0.4f") %
             flatness_thr_[0] % flatness_thr_[1] % flatness_thr_[2] % flatness_thr_[3])
                .str()
         << endl;
  }

  return;
}

template <typename PointT>
inline void PatchWorkpp<PointT>::temporal_ground_revert(
    pcl::PointCloud<PointT>& cloud_ground, pcl::PointCloud<PointT>& cloud_nonground,
    std::vector<double> ring_flatness, std::vector<RevertCandidate<PointT>> candidates,
    int concentric_idx)
{
  if (verbose_)
    std::cout << "\033[1;34m"
              << "=========== Temporal Ground Revert (TGR) ==========="
              << "\033[0m" << endl;

  double mean_flatness = 0.0, stdev_flatness = 0.0;
  calc_mean_stdev(ring_flatness, mean_flatness, stdev_flatness);

  if (verbose_)
  {
    cout << "[" << candidates[0].concentric_idx << ", " << candidates[0].sector_idx << "]"
         << " mean_flatness: " << mean_flatness << ", stdev_flatness: " << stdev_flatness
         << std::endl;
  }

  for (size_t i = 0; i < candidates.size(); i++)
  {
    RevertCandidate<PointT> candidate = candidates[i];

    // Debug
    if (verbose_)
    {
      cout << "\033[1;33m" << candidate.sector_idx << "th flat_sector_candidate"
           << " / flatness: " << candidate.ground_flatness
           << " / line_variable: " << candidate.line_variable
           << " / ground_num : " << candidate.regionwise_ground.size() << "\033[0m"
           << endl;
    }

    double mu_flatness = mean_flatness + 1.5 * stdev_flatness;
    double prob_flatness =
        1 / (1 + exp((candidate.ground_flatness - mu_flatness) / (mu_flatness / 10)));

    if (candidate.regionwise_ground.size() > 1500 &&
        candidate.ground_flatness < th_dist_ * th_dist_)
      prob_flatness = 1.0;

    double prob_line = 1.0;
    if (candidate.line_variable >
        8.0)   //&& candidate.line_dir > M_PI/4)// candidate.ground_elevation > elevation_thr_[concentric_idx])
    {
      // if (verbose_) cout << "line_dir: " << candidate.line_dir << endl;
      prob_line = 0.0;
    }

    bool revert = prob_line * prob_flatness > 0.5;

    if (concentric_idx < num_rings_of_interest_)
    {
      if (revert)
      {
        if (verbose_)
        {
          cout << "\033[1;32m"
               << "REVERT TRUE"
               << "\033[0m" << endl;
        }

        revert_pc_ += candidate.regionwise_ground;
        cloud_ground += candidate.regionwise_ground;
      }
      else
      {
        if (verbose_)
        {
          cout << "\033[1;31m"
               << "FINAL REJECT"
               << "\033[0m" << endl;
        }
        reject_pc_ += candidate.regionwise_ground;
        cloud_nonground += candidate.regionwise_ground;
      }
    }
  }

  if (verbose_)
    std::cout << "\033[1;34m"
              << "===================================================="
              << "\033[0m" << endl;
}

// For adaptive
template <typename PointT>
inline void PatchWorkpp<PointT>::extract_piecewiseground(
    const int zone_idx, const pcl::PointCloud<PointT>& src, pcl::PointCloud<PointT>& dst,
    pcl::PointCloud<PointT>& non_ground_dst)
{
  // 0. Initialization
  if (!ground_pc_.empty())
    ground_pc_.clear();
  if (!dst.empty())
    dst.clear();
  if (!non_ground_dst.empty())
    non_ground_dst.clear();

  // 1. Region-wise Vertical Plane Fitting (R-VPF)
  // : removes potential vertical plane under the ground plane
  pcl::PointCloud<PointT> src_wo_verticals;
  src_wo_verticals = src;

  if (enable_RVPF_)
  {
    for (int i = 0; i < num_iter_; i++)
    {
      extract_initial_seeds(zone_idx, src_wo_verticals, ground_pc_, th_seeds_v_);
      estimate_plane(ground_pc_);

      if (zone_idx == 0 && normal_(2) < uprightness_thr_)
      {
        pcl::PointCloud<PointT> src_tmp;
        src_tmp = src_wo_verticals;
        src_wo_verticals.clear();

        Eigen::MatrixXf points(src_tmp.points.size(), 3);
        int j = 0;
        for (auto& p : src_tmp.points)
        {
          points.row(j++) << p.x, p.y, p.z;
        }
        // ground plane model
        Eigen::VectorXf result = points * normal_;

        for (int r = 0; r < result.rows(); r++)
        {
          if (result[r] < th_dist_v_ - d_ && result[r] > -th_dist_v_ - d_)
          {
            non_ground_dst.points.push_back(src_tmp[r]);
            vertical_pc_.points.push_back(src_tmp[r]);
          }
          else
          {
            src_wo_verticals.points.push_back(src_tmp[r]);
          }
        }
      }
      else
        break;
    }
  }

  extract_initial_seeds(zone_idx, src_wo_verticals, ground_pc_);
  estimate_plane(ground_pc_);

  // 2. Region-wise Ground Plane Fitting (R-GPF)
  // : fits the ground plane

  //pointcloud to matrix
  Eigen::MatrixXf points(src_wo_verticals.points.size(), 3);
  int j = 0;
  for (auto& p : src_wo_verticals.points)
  {
    points.row(j++) << p.x, p.y, p.z;
  }

  for (int i = 0; i < num_iter_; i++)
  {
    ground_pc_.clear();

    // ground plane model
    Eigen::VectorXf result = points * normal_;
    // threshold filter
    for (int r = 0; r < result.rows(); r++)
    {
      if (i < num_iter_ - 1)
      {
        if (result[r] < th_dist_ - d_)
        {
          ground_pc_.points.push_back(src_wo_verticals[r]);
        }
      }
      else
      {   // Final stage
        if (result[r] < th_dist_ - d_)
        {
          dst.points.push_back(src_wo_verticals[r]);
        }
        else
        {
          non_ground_dst.points.push_back(src_wo_verticals[r]);
        }
      }
    }

    if (i < num_iter_ - 1)
      estimate_plane(ground_pc_);
    else
      estimate_plane(dst);
  }
}

// template<typename PointT> inline
// geometry_msgs::PolygonStamped PatchWorkpp<PointT>::set_polygons(int zone_idx, int r_idx, int theta_idx, int num_split) {
//     geometry_msgs::PolygonStamped polygons;
//     // Set point of polygon. Start from RL and ccw
//     geometry_msgs::Point32 point;

//     // RL
//     double zone_min_range = min_ranges_[zone_idx];
//     double r_len = r_idx * ring_sizes_[zone_idx] + zone_min_range;
//     double angle = theta_idx * sector_sizes_[zone_idx];

//     point.x = r_len * cos(angle);
//     point.y = r_len * sin(angle);
//     point.z = MARKER_Z_VALUE;
//     polygons.polygon.points.push_back(point);
//     // RU
//     r_len = r_len + ring_sizes_[zone_idx];
//     point.x = r_len * cos(angle);
//     point.y = r_len * sin(angle);
//     point.z = MARKER_Z_VALUE;
//     polygons.polygon.points.push_back(point);

//     // RU -> LU
//     for (int idx = 1; idx <= num_split; ++idx) {
//         angle = angle + sector_sizes_[zone_idx] / num_split;
//         point.x = r_len * cos(angle);
//         point.y = r_len * sin(angle);
//         point.z = MARKER_Z_VALUE;
//         polygons.polygon.points.push_back(point);
//     }

//     r_len = r_len - ring_sizes_[zone_idx];
//     point.x = r_len * cos(angle);
//     point.y = r_len * sin(angle);
//     point.z = MARKER_Z_VALUE;
//     polygons.polygon.points.push_back(point);

//     for (int idx = 1; idx < num_split; ++idx) {
//         angle = angle - sector_sizes_[zone_idx] / num_split;
//         point.x = r_len * cos(angle);
//         point.y = r_len * sin(angle);
//         point.z = MARKER_Z_VALUE;
//         polygons.polygon.points.push_back(point);
//     }

//     return polygons;
// }

// template<typename PointT> inline
// void PatchWorkpp<PointT>::set_ground_likelihood_estimation_status(
//         const int zone_idx, const int ring_idx,
//         const int concentric_idx,
//         const double z_vec,
//         const double z_elevation,
//         const double ground_flatness) {
//     if (z_vec > uprightness_thr_) { //orthogonal
//         if (concentric_idx < num_rings_of_interest_) {
//             if (z_elevation > elevation_thr_[concentric_idx]) {
//                 if (flatness_thr_[concentric_idx] > ground_flatness) {
//                     poly_list_.likelihood.push_back(FLAT_ENOUGH);
//                 } else {
//                     poly_list_.likelihood.push_back(TOO_HIGH_ELEVATION);
//                 }
//             } else {
//                 poly_list_.likelihood.push_back(UPRIGHT_ENOUGH);
//             }
//         } else {
//             poly_list_.likelihood.push_back(UPRIGHT_ENOUGH);
//         }
//     } else { // tilted
//         poly_list_.likelihood.push_back(TOO_TILTED);
//     }
// }

template <typename PointT>
inline void
PatchWorkpp<PointT>::calc_mean_stdev(std::vector<double> vec, double& mean, double& stdev)
{
  if (vec.size() <= 1)
    return;

  mean = std::accumulate(vec.begin(), vec.end(), 0.0) / vec.size();

  for (int i = 0; i < vec.size(); i++)
  {
    stdev += (vec.at(i) - mean) * (vec.at(i) - mean);
  }
  stdev /= vec.size() - 1;
  stdev = sqrt(stdev);
}

template <typename PointT>
inline double PatchWorkpp<PointT>::xy2theta(const double& x, const double& y)
{   // 0 ~ 2 * PI
  // if (y >= 0) {
  //     return atan2(y, x); // 1, 2 quadrant
  // } else {
  //     return 2 * M_PI + atan2(y, x);// 3, 4 quadrant
  // }

  double angle = atan2(y, x);
  return angle > 0 ? angle : 2 * M_PI + angle;
}

template <typename PointT>
inline double PatchWorkpp<PointT>::xy2radius(const double& x, const double& y)
{
  return sqrt(pow(x, 2) + pow(y, 2));
}

template <typename PointT>
inline void PatchWorkpp<PointT>::callbackCloud(
    const sensor_msgs::msg::PointCloud2::ConstSharedPtr& cloud_msg)
{
  double time_taken;

  pcl::PointCloud<PointT> pc_curr;
  pcl::PointCloud<PointT> pc_ground;
  pcl::PointCloud<PointT> pc_non_ground;

  pcl::fromROSMsg(*cloud_msg, pc_curr);

  estimate_ground(pc_curr, pc_ground, pc_non_ground, time_taken);

  RCLCPP_INFO_STREAM(
      rclcpp::get_logger("patchworkpp"), "\033[1;32m"
                                             << "Input PointCloud: " << pc_curr.size()
                                             << " -> Ground: " << pc_ground.size()
                                             << "/ NonGround: " << pc_non_ground.size()
                                             << " (running_time: " << time_taken
                                             << " sec)"
                                             << "\033[0m");

  pub_cloud->publish(
      cloud2msg(pc_curr, cloud_msg->header.stamp, cloud_msg->header.frame_id));
  pub_ground->publish(
      cloud2msg(pc_ground, cloud_msg->header.stamp, cloud_msg->header.frame_id));
  pub_non_ground->publish(
      cloud2msg(pc_non_ground, cloud_msg->header.stamp, cloud_msg->header.frame_id));
}

template <typename PointT>
sensor_msgs::msg::PointCloud2 PatchWorkpp<PointT>::cloud2msg(
    pcl::PointCloud<PointT> cloud, const rclcpp::Time& stamp, std::string frame_id)
{
  sensor_msgs::msg::PointCloud2 cloud_ROS;
  pcl::toROSMsg(cloud, cloud_ROS);
  cloud_ROS.header.stamp = stamp;
  cloud_ROS.header.frame_id = frame_id;
  return cloud_ROS;
}

template <typename PointT>
inline void PatchWorkpp<PointT>::pc2czm(
    const pcl::PointCloud<PointT>& src, std::vector<Zone>& czm,
    pcl::PointCloud<PointT>& cloud_nonground)
{
  for (int i = 0; i < src.size(); i++)
  {
    if ((!noise_idxs_.empty()) && (i == noise_idxs_.front()))
    {
      noise_idxs_.pop();
      continue;
    }

    PointT pt = src.points[i];

    double r = xy2radius(pt.x, pt.y);
    if ((r <= max_range_) && (r > min_range_))
    {
      double theta = xy2theta(pt.x, pt.y);

      int zone_idx = num_zones_ - 1;
      for (int i = 1; i < num_zones_ - 1; i++)
      {
        if (r < min_ranges_.at(i))
          zone_idx = i - 1;
      }

      int ring_idx = min(
          static_cast<long>(((r - min_ranges_.at(zone_idx)) / ring_sizes_.at(zone_idx))),
          num_rings_each_zone_.at(zone_idx) - 1);
      int sector_idx =
          min(static_cast<long>((theta / sector_sizes_.at(zone_idx))),
              num_sectors_each_zone_.at(zone_idx) - 1);

      czm[zone_idx][ring_idx][sector_idx].points.emplace_back(pt);
    }
    else
    {
      cloud_nonground.push_back(pt);
    }
  }

  if (verbose_)
    cout << "[ CZM ] Divides pointcloud into the concentric zone model" << endl;
}

#endif
