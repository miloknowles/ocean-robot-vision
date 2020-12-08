#pragma once

#include <cmath>

#include "line_descriptor/include/line_descriptor_custom.hpp"

#include "core/eigen_types.hpp"
#include "core/pinhole_camera.hpp"

namespace ld = cv::line_descriptor;

namespace bm {
namespace core {

static const double DEG_TO_RAD_D = M_PI / 180.0;
static const double RAD_TO_DEG_D = 180.0 / M_PI;


// Return the unit direction vector.
inline Vector2d NormalizedDirection(const ld::KeyLine& kl)
{
  const cv::Point2d diff = kl.getEndPoint() - kl.getStartPoint();
  const Vector2d v(diff.x, diff.y);
  return v.normalized();
}


// Returns the unit direction vectors for a list of line segments.
inline std::vector<Vector2d> NormalizedDirection(const std::vector<ld::KeyLine>& kls)
{
  std::vector<Vector2d> out(kls.size());
  for (int i = 0; i < kls.size(); ++i) {
    out.at(i) = NormalizedDirection(kls.at(i));
  }
  return out;
}

inline double DegToRad(const double deg)
{
  return deg * DEG_TO_RAD_D;
}

inline double RadToDeg(const double rad)
{
  return rad * RAD_TO_DEG_D;
}

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Matrix3d skew(Vector3d v);

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Matrix3d fast_skewexp(Vector3d v);

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Vector3d skewcoords(Matrix3d M);

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Matrix4d inverse_se3(Matrix4d T);

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Matrix4d expmap_se3(Vector6d x);

// From: https://github.com/rubengooj/stvo-pl/blob/master/src/auxiliar.cpp
Vector6d logmap_se3(Matrix4d T);


// Project a 3D point from the 'world' frame to the image plane of the camera.
inline Vector2d ProjectWorldPoint(const PinholeCamera& camera,
                                  const Matrix4d& T_world_cam,
                                  const Vector3d& P_world)
{
  return camera.Project(T_world_cam.block<3, 3>(0, 0) * P_world + T_world_cam.block<3, 1>(0, 3));
}


// Transform a 3D point from the 'ref' frame to the 'target' frame.
inline Vector3d ApplyTransform(const Matrix4d& T_ref_target, const Vector3d& P_ref)
{
  return T_ref_target.block<3, 3>(0, 0) * P_ref + T_ref_target.block<3, 1>(0, 3);
}


// Returns the rotation of 1 in 0.
inline Matrix3d RelativeRotation(const Matrix3d& R_0_w, const Matrix3d& R_1_w)
{
  return R_0_w.transpose() * R_1_w;
}

// Grabs the items from v based on indices.
template <typename T>
inline std::vector<T> Subset(const std::vector<T>& v, const std::vector<int>& indices)
{
  std::vector<T> out;
  for (int i : indices) {
    out.emplace_back(v.at(i));
  }
  return out;
}


inline void FillMask(const std::vector<int> indices, std::vector<char>& mask)
{
  std::fill(mask.begin(), mask.end(), false);
  for (int i : indices) { mask.at(i) = true; }
}

}
}
