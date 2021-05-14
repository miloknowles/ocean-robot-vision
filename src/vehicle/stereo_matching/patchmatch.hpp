#pragma once

#include "core/macros.hpp"
#include "params/params_base.hpp"
#include "params/yaml_parser.hpp"
#include "vision_core/cv_types.hpp"

#include "feature_tracking/feature_detector.hpp"
#include "feature_tracking/stereo_matcher.hpp"

namespace bm {
namespace stereo {

using namespace core;


typedef std::function<float(const Image1b&, const Image1b&)> CostFunctor;


// Returns a binary mask where "1" indicates foreground and "0" indicates background.
void ForegroundTextureMask(const Image1b& gray,
                          Image1b& mask,
                          int ksize = 7,
                          double min_grad = 35.0,
                          int downsize = 2);


class Patchmatch final {
 public:
  struct Params final : public ParamsBase {
    MACRO_PARAMS_STRUCT_CONSTRUCTORS(Params);

    ft::FeatureDetector::Params detector_params;
    ft::StereoMatcher::Params matcher_params;

   private:
    void LoadParams(const YamlParser& p) override;
  };

  MACRO_DELETE_COPY_CONSTRUCTORS(Patchmatch);

  Patchmatch(const Params& params)
      : params_(params),
        detector_(params.detector_params),
        matcher_(params.matcher_params) {}

  Image1f EstimateDisparity(const Image1b& iml,
                            const Image1b& imr);

  Image1f Initialize(const Image1b& iml,
                     const Image1b& imr,
                     int downsample_factor);

  void Propagate(const Image1b& iml,
                 const Image1b& imr,
                 Image1f& disp,
                 const CostFunctor& f,
                 int patch_height,
                 int patch_width);

 private:
  Params params_;

  ft::FeatureDetector detector_;
  ft::StereoMatcher matcher_;
};


}
}