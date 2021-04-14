#include <glog/logging.h>

#include <lcm/lcm-cpp.hpp>

#include <utility>
#include <unordered_map>

#include <opencv2/highgui.hpp>

#include "core/eigen_types.hpp"
#include "core/macros.hpp"
#include "core/params_base.hpp"
#include "core/pinhole_camera.hpp"
#include "core/stereo_camera.hpp"
#include "core/timer.hpp"
#include "core/uid.hpp"
#include "core/file_utils.hpp"
#include "core/path_util.hpp"
#include "core/data_subsampler.hpp"

#include "dataset/dataset_util.hpp"

#include "vio/state_estimator.hpp"
#include "vio/visualizer_3d.hpp"

#include "lcm_util/util_pose3_t.hpp"
#include "lcm_util/util_imu_measurement_t.hpp"
#include "lcm_util/util_depth_measurement_t.hpp"
#include "lcm_util/util_range_measurement_t.hpp"
#include "lcm_util/decode_image.hpp"

#include "feature_tracking/visualization_2d.hpp"

#include "vehicle/pose3_stamped_t.hpp"
#include "vehicle/stereo_image_t.hpp"
#include "vehicle/imu_measurement_t.hpp"
#include "vehicle/range_measurement_t.hpp"
#include "vehicle/depth_measurement_t.hpp"

using namespace bm;
using namespace core;
using namespace vio;


class StateEstimatorLcm final {
 public:
  struct Params : public ParamsBase
  {
    MACRO_PARAMS_STRUCT_CONSTRUCTORS(Params);

    bool use_stereo = true;
    bool use_imu = true;
    bool use_depth = true;
    bool use_range = true;

    std::string channel_input_stereo;
    std::string channel_input_imu;
    std::string channel_input_range;
    std::string channel_input_depth;

    std::string channel_output_filter_pose;
    std::string channel_output_smoother_pose;

    bool visualize = true;
    float filter_publish_hz = 50.0;

    StateEstimator::Params state_estimator_params;
    Visualizer3D::Params visualizer3d_params;

   private:
    void LoadParams(const YamlParser& parser) override
    {
      parser.GetYamlParam("use_stereo", &use_stereo);
      parser.GetYamlParam("use_imu", &use_imu);
      parser.GetYamlParam("use_depth", &use_depth);
      parser.GetYamlParam("use_range", &use_range);

      channel_input_stereo = YamlToString(parser.GetYamlNode("channel_input_stereo"));
      channel_input_imu = YamlToString(parser.GetYamlNode("channel_input_imu"));
      channel_input_depth = YamlToString(parser.GetYamlNode("channel_input_depth"));
      channel_input_range = YamlToString(parser.GetYamlNode("channel_input_range"));

      channel_output_filter_pose = YamlToString(parser.GetYamlNode("channel_output_filter_pose"));
      channel_output_smoother_pose = YamlToString(parser.GetYamlNode("channel_output_smoother_pose"));

      parser.GetYamlParam("visualize", &visualize);
      parser.GetYamlParam("filter_publish_hz", &filter_publish_hz);

      state_estimator_params = StateEstimator::Params(parser.Subtree("StateEstimator"));
      visualizer3d_params = Visualizer3D::Params(parser.Subtree("Visualizer3D"));
    }
  };

  StateEstimatorLcm(const Params& params)
      : params_(params),
        state_estimator_(params.state_estimator_params),
        viz_(params.visualizer3d_params),
        filter_subsampler_(params.filter_publish_hz)
  {
    if (!lcm_.good()) {
      LOG(WARNING) << "Failed to initialize LCM" << std::endl;
      return;
    }

    // TODO(milo): Where should this come from? External function call?
    gtsam::Pose3 P0_world_body = gtsam::Pose3::identity();
    state_estimator_.Initialize(0, P0_world_body);

    if (params_.visualize) {
      viz_.Start();
      viz_.UpdateBodyPose("T0_world_body", P0_world_body.matrix());
      viz_.SetViewerPose(P0_world_body.matrix());
    }

    state_estimator_.RegisterSmootherResultCallback(std::bind(&StateEstimatorLcm::SmootherCallback, this, std::placeholders::_1));
    state_estimator_.RegisterFilterResultCallback(std::bind(&StateEstimatorLcm::FilterCallback, this, std::placeholders::_1));

    lcm_.subscribe(params_.channel_input_stereo, &StateEstimatorLcm::HandleStereo, this);
    lcm_.subscribe(params_.channel_input_imu, &StateEstimatorLcm::HandleImu, this);
    lcm_.subscribe(params_.channel_input_range, &StateEstimatorLcm::HandleRange, this);
    lcm_.subscribe(params_.channel_input_depth, &StateEstimatorLcm::HandleDepth, this);
  }

  // Blocks to keep this node alive.
  void Spin()
  {
    while (!is_shutdown_) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void HandleStereo(const lcm::ReceiveBuffer*,
                    const std::string&,
                    const vehicle::stereo_image_t* msg)
  {
    if (!params_.use_stereo) { return; }
    CHECK_EQ(msg->img_left.encoding, msg->img_right.encoding)
        << "Left and right images have different encodings!" << std::endl;

    const std::string encoding = msg->img_left.encoding;

    StereoImage1b stereo_pair(msg->header.timestamp, msg->header.seq, Image1b(), Image1b());

    if (encoding == "jpg") {
      bm::DecodeJPG(msg->img_left, stereo_pair.left_image);
      bm::DecodeJPG(msg->img_right, stereo_pair.right_image);

      if (stereo_pair.left_image.rows == 0 || stereo_pair.left_image.cols == 0) {
        LOG(WARNING) << "Problem decoding left image" << std::endl;
        return;
      }
      if (stereo_pair.right_image.rows == 0 || stereo_pair.right_image.cols == 0) {
        LOG(WARNING) << "Problem decoding right image" << std::endl;
        return;
      }
    } else {
      LOG(WARNING) << "Unsupported encoding: " << encoding << std::endl;
    }

    state_estimator_.ReceiveStereo(std::move(stereo_pair));
  }

  void HandleImu(const lcm::ReceiveBuffer*,
                 const std::string&,
                 const vehicle::imu_measurement_t* msg)
  {
    if (!params_.use_imu) { return; }
    ImuMeasurement data;
    decode_imu_measurement_t(*msg, data);
    state_estimator_.ReceiveImu(std::move(data));
  }

  void HandleDepth(const lcm::ReceiveBuffer*,
                   const std::string&,
                   const vehicle::depth_measurement_t* msg)
  {
    if (!params_.use_depth) { return; }
    DepthMeasurement data(0, 123);
    decode_depth_measurement_t(*msg, data);
    state_estimator_.ReceiveDepth(std::move(data));
  }

  void HandleRange(const lcm::ReceiveBuffer*,
                   const std::string&,
                   const vehicle::range_measurement_t* msg)
  {
    if (!params_.use_range) { return; }
    RangeMeasurement data(0, 0, Vector3d::Zero());
    decode_range_measurement_t(*msg, data);
    state_estimator_.ReceiveRange(std::move(data));
  }

  void SmootherCallback(const SmootherResult& result)
  {
    const core::uid_t cam_id = static_cast<core::uid_t>(result.keypose_id);
    const Matrix3d body_cov_pose = result.cov_pose.block<3, 3>(3, 3);
    const Matrix3d world_R_body = result.world_P_body.rotation().matrix();
    const Matrix3d world_cov_pose = world_R_body * body_cov_pose * world_R_body.transpose();
    viz_.AddCameraPose(cam_id, Image1b(), result.world_P_body.matrix(), true, std::make_shared<Matrix3d>(world_cov_pose));

    // Publish pose estimate to LCM.
    vehicle::pose3_stamped_t msg;
    msg.header.timestamp = ConvertToNanoseconds(result.timestamp);
    msg.header.seq = -1;
    msg.header.frame_id = "imu0";
    pack_pose3_t(result.world_P_body, msg.pose);

    lcm_.publish(params_.channel_output_smoother_pose, &msg);
  }

  void FilterCallback(const StateStamped& ss)
  {
    // Limit the publishing rate to avoid overwhelming consumers.
    if (!filter_subsampler_.ShouldSample(ConvertToNanoseconds(ss.timestamp))) {
      return;
    }

    Matrix4d world_T_body = Matrix4d::Identity();
    world_T_body.block<3, 3>(0, 0) = ss.state.q.toRotationMatrix();
    world_T_body.block<3, 1>(0, 3) = ss.state.t;
    viz_.UpdateBodyPose("imu0", world_T_body);

    // Publish pose estimate to LCM.
    vehicle::pose3_stamped_t msg;
    msg.header.timestamp = ConvertToNanoseconds(ss.timestamp);
    msg.header.seq = -1;
    msg.header.frame_id = "imu0";
    pack_pose3_t(ss.state.q, ss.state.t, msg.pose);

    lcm_.publish(params_.channel_output_filter_pose, &msg);
  }

 private:
  std::atomic_bool is_shutdown_{false};

  Params params_;
  lcm::LCM lcm_;
  StateEstimator state_estimator_;
  Visualizer3D viz_;

  DataSubsampler filter_subsampler_;
};


int main(int argc, char const *argv[])
{
  // Set up glog.
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  CHECK_EQ(3ul, argc)
      << "Requires (2) args: node_params_path and shared_params_path."
      << "They should be relative to vehicle/config" << std::endl;

  std::string node_params_path = std::string(argv[1]);
  const std::string shared_params_path = std::string(argv[2]);

  StateEstimatorLcm::Params params(
    config_path(node_params_path),
    config_path(shared_params_path));

  StateEstimatorLcm node(params);
  node.Spin();

  LOG(INFO) << "DONE" << std::endl;

  return 0;
}