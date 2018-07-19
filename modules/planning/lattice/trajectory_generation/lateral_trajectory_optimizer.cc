/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/**
 * @file
 **/

#include "modules/planning/lattice/trajectory_generation/lateral_trajectory_optimizer.h"

#include <utility>

#include "glog/logging.h"
#include "modules/planning/lattice/trajectory1d/constant_jerk_trajectory1d.h"

namespace apollo {
namespace planning {

LateralTrajectoryOptimizer::LateralTrajectoryOptimizer(
    const double d_init, const double d_prime_init, const double d_pprime_init,
    const double delta_s, std::vector<std::pair<double, double>> d_bounds) :
    opt_piecewise_trajectory_(d_init, d_prime_init, d_pprime_init) {
  num_of_points_ = d_bounds.size();

  num_of_variables_ = 3 * num_of_points_;

  delta_s_ = delta_s;

  d_bounds_ = std::move(d_bounds);

  w_d_ = 1.0;

  w_d_prime_ = 1.0;

  w_d_pprime_ = 1.0;

  w_d_obs_ = 1.0;
}

bool LateralTrajectoryOptimizer::get_nlp_info(int& n, int& m,
    int& nnz_jac_g, int& nnz_h_lag, IndexStyleEnum& index_style) {
  // variables
  n = num_of_points_ * 3;

  // constraints
  m = num_of_points_ * 3;

  // none zero hessian and lagrangian
  nnz_h_lag = n;

  index_style = IndexStyleEnum::C_STYLE;

  return true;
}

bool LateralTrajectoryOptimizer::get_bounds_info(int n, double* x_l,
    double* x_u, int m, double* g_l, double* g_u) {

  const double LARGE_VALUE = 10.0;

  // bounds for variables
  // d bounds;
  for (std::size_t i = 0; i < num_of_points_; ++i) {
    x_l[i] = d_bounds_[i].first;
    x_u[i] = d_bounds_[i].second;
  }

  // d_prime bounds
  std::size_t offset = num_of_points_;
  for (std::size_t i = 0; i < num_of_points_; ++i) {
    x_l[offset + i] = -LARGE_VALUE;
    x_u[offset + i] = LARGE_VALUE;
  }

  // d_pprime bounds;
  offset = 2 * num_of_points_;
  for (std::size_t i = 0; i < num_of_points_; ++i) {
    x_l[offset + i] = -LARGE_VALUE;
    x_u[offset + i] = LARGE_VALUE;
  }

  // bounds for constraints

  // jerk bounds
  for (std::size_t i = 0; i + 1 < num_of_points_; ++i) {
    g_l[i] = -d_ppprime_max_ * delta_s_;
    g_u[i] = d_ppprime_max_ * delta_s_;
  }

  // velocity increment
  offset = num_of_points_ - 1;
  for (std::size_t i = 0; i + 1 < num_of_points_; ++i) {
    g_l[offset + i] = g_u[offset + i] = 0.0;
  }

  // position increment
  offset = 2 * (num_of_points_ - 1);
  for (std::size_t i = 0; i + 1 < num_of_points_; ++i) {
    g_l[offset + i] = g_u[offset + i] = 0.0;
  }

  offset = 3 * (num_of_points_ - 1);
  // d_init
  g_l[offset] = g_u[offset] = 0.0;

  // d_prime_init
  g_l[offset + 1] = g_u[offset + 1] = 0.0;

  // d_pprime_init
  g_l[offset + 2] = g_u[offset + 2] = 0.0;

  return true;
}

bool LateralTrajectoryOptimizer::get_starting_point(int n, bool init_x,
    double* x, bool init_z, double* z_L, double* z_U, int m, bool init_lambda,
    double* lambda) {

  CHECK_EQ(std::size_t(n), num_of_points_ * 3);
  CHECK(init_x == true);
  CHECK(init_z == false);
  CHECK(init_lambda == false);

  for (std::size_t i = 0; i < num_of_points_; ++i) {
    x[i] = 0.0;
    x[num_of_points_ + i] = 0.0;
    x[num_of_points_ + num_of_points_ + i] = 0.0;
  }

  x[0] = d_init_;
  x[num_of_points_] = d_prime_init_;
  x[num_of_points_ + num_of_points_] = d_pprime_init_;
  return false;
}

bool LateralTrajectoryOptimizer::eval_f(int n, const double* x,
    bool new_x, double& obj_value) {
  obj_value = 0.0;

  std::size_t offset_prime = num_of_points_;
  std::size_t offset_pprime = 2 * num_of_points_;
  for (std::size_t i = 0; i < num_of_points_; ++i) {
    obj_value += x[i] * x[i] * w_d_;
    obj_value += x[offset_prime + i] * x[offset_prime + i] * w_d_prime_;
    obj_value += x[offset_pprime + i] * x[offset_pprime + i] * w_d_pprime_;

    auto dist = x[i] - (d_bounds_[i].first + d_bounds_[i].second) * 0.5;
    obj_value += dist * dist * w_d_obs_;
  }

  return true;
}

bool LateralTrajectoryOptimizer::eval_grad_f(int n, const double* x,
    bool new_x, double* grad_f) {

  std::fill(grad_f, grad_f + n, 0.0);

  std::size_t offset_prime = num_of_points_;
  std::size_t offset_pprime = 2 * num_of_points_;
  for (std::size_t i = 0; i < num_of_points_; ++i) {
    auto obs_center = (d_bounds_[i].first + d_bounds_[i].second) * 0.5;
    grad_f[i] = 2.0 * x[i] * w_d_ + 2.0 * (x[i] - obs_center) * w_d_obs_;

    grad_f[offset_prime + i] = 2.0 * x[offset_prime + i] * w_d_prime_;

    grad_f[offset_pprime + i] = 2.0 * x[offset_pprime + i] * w_d_pprime_;
  }

  return true;
}

bool LateralTrajectoryOptimizer::eval_g(int n, const double* x,
    bool new_x, int m, double* g) {

  std::size_t offset_prime = num_of_points_;
  std::size_t offset_pprime = 2 * num_of_points_;

  for (std::size_t i = 0; i + 1 < num_of_points_; ++i) {
    g[i] = x[offset_pprime + i + 1] - x[offset_pprime + i];
  }

  for (std::size_t i = 0; i + 1 < num_of_points_; ++i) {
    double p0 = x[i];
    double v0 = x[offset_prime + i];
    double a0 = x[offset_pprime + i];

    double p1 = x[i + 1];
    double v1 = x[offset_prime + i + 1];
    double a1 = x[offset_pprime + i + 1];

    double j = (a1 - a0) / delta_s_;
    ConstantJerkTrajectory1d t(p0, v0, a0, j, delta_s_);

    g[num_of_points_ - 1 + i] = t.end_velocity() - v1;
    g[2 * (num_of_points_ - 1) + i] = t.end_position() - p1;
  }

  std::size_t offset = 3 * (num_of_points_ - 1);
  g[offset] = x[0] - d_init_;
  g[offset + 1] = x[offset_prime] - d_prime_init_;
  g[offset + 2] = x[offset_pprime] - d_pprime_init_;
  return true;
}

bool LateralTrajectoryOptimizer::eval_jac_g(int n, const double* x,
    bool new_x, int m, int nele_jac, int* iRow, int* jCol, double* values) {

  CHECK_EQ(std::size_t(n), num_of_points_ * 3);
  CHECK_EQ(std::size_t(m), num_of_points_ * 3);

  if (values == NULL) {
    std::size_t nz_index = 0;
    std::size_t constraint_index = 0;

    // acc constraint
    // d_i'' - d_i+1''
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      // d_i''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index;
      ++nz_index;
      // d_i+1''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index + 1;
      ++nz_index;

      ++constraint_index;
    }

    // velocity constraint
    // d_i' - d_i+1 + 0.5 * ds * (d_i'' + d_i+1'')
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      // d_i'
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = num_of_points_ + variable_index;
      ++nz_index;
      // d_i+1'
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = num_of_points_ + variable_index + 1;
      ++nz_index;
      // d_i''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index;
      ++nz_index;
      // d_i+1''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index + 1;
      ++nz_index;

      ++constraint_index;
    }

    // state constraint
    // d_i - d_i+1 + d_i' * ds + 1/3 * d_i'' * ds^2 + 1/6 * d_i+1'' * ds^2
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      // d_i
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = variable_index;
      ++nz_index;
      // d_i+1
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = variable_index + 1;
      ++nz_index;
      // d_i'
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = num_of_points_ + variable_index;
      ++nz_index;
      // d_i''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index;
      ++nz_index;
      // d_i+1''
      iRow[nz_index] = constraint_index;
      jCol[nz_index] = 2 * num_of_points_ + variable_index + 1;
      ++nz_index;

      ++constraint_index;
    }

    // initial state constraint
    // d_0
    iRow[nz_index] = constraint_index;
    jCol[nz_index] = 0;
    ++nz_index;
    // d_0'
    iRow[nz_index] = constraint_index;
    jCol[nz_index] = num_of_points_;
    ++nz_index;
    // d_0''
    iRow[nz_index] = constraint_index;
    jCol[nz_index] = 2 * num_of_points_;
    ++nz_index;

    nnz_jac_g_ = nz_index;
  } else {
    if (new_x) {
      // TODO(kechxu) update
    }

    std::fill(values, values + nnz_jac_g_, 0.0);
    // first, positional equality constraints
    std::size_t nz_index = 0;

    // fill acc constraint
    // d_i'' - d_i+1''
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      values[nz_index] = 1.0;
      ++nz_index;

      values[nz_index] = -1.0;
      ++nz_index;
    }

    // fill velocity constraint
    // d_i' - d_i+1 + 0.5 * ds * (d_i'' + d_i+1'')
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      // d_i'
      values[nz_index] = 1.0;
      ++nz_index;
      // d_i+1'
      values[nz_index] = -1.0;
      ++nz_index;
      // d_i''
      values[nz_index] = 0.5 * delta_s_;
      ++nz_index;
      // d_i+1''
      values[nz_index] = 0.5 * delta_s_;
      ++nz_index;
    }

    // state constraint
    // d_i - d_i+1 + d_i' * ds + 1/3 * d_i'' * ds^2 + 1/6 * d_i+1'' * ds^2
    for (std::size_t variable_index = 0; variable_index + 1 < num_of_points_;
         ++variable_index) {
      // d_i
      values[nz_index] = 1.0;
      ++nz_index;
      // d_i+1
      values[nz_index] = -1.0;
      ++nz_index;
      // d_i'
      values[nz_index] = delta_s_;
      ++nz_index;
      // d_i''
      values[nz_index] = delta_s_ * delta_s_ / 3.0;
      ++nz_index;
      // d_i+1''
      values[nz_index] = delta_s_ * delta_s_ / 6.0;
      ++nz_index;
    }

    // initial state constraint
    for (std::size_t order = 0; order < 3; ++order) {
      values[nz_index] = 1.0;
      ++nz_index;
    }

    CHECK_EQ(nz_index, nnz_jac_g_);
  }
  return true;
}

bool LateralTrajectoryOptimizer::eval_h(int n, const double* x,
    bool new_x, double obj_factor, int m, const double* lambda, bool new_lambda,
    int nele_hess, int* iRow, int* jCol, double* values) {
  if (values == nullptr) {
    for (std::size_t i = 0; i < 3 * num_of_points_; ++i) {
      iRow[i] = i;
      jCol[i] = i;
    }
  } else {
    for (std::size_t i = 0; i < num_of_points_; ++i) {
      values[i] = 4.0;
    }

    for (std::size_t i = num_of_points_; i < 3 * num_of_points_; ++i) {
      values[i] = 2.0;
    }
  }
  return true;
}

void LateralTrajectoryOptimizer::finalize_solution(
    Ipopt::SolverReturn status, int n, const double* x, const double* z_L,
    const double* z_U, int m, const double* g, const double* lambda,
    double obj_value, const Ipopt::IpoptData* ip_data,
    Ipopt::IpoptCalculatedQuantities* ip_cq) {

  std::size_t offset = num_of_points_ * 2;
  for (std::size_t i = 1; i < num_of_points_; ++i) {
    auto j = (x[offset] - x[offset - 1]) / delta_s_;
    opt_piecewise_trajectory_.AppendSegment(j, delta_s_);
  }
}

PiecewiseJerkTrajectory1d
LateralTrajectoryOptimizer::GetOptimalTrajectory() const {
  return opt_piecewise_trajectory_;
}

}  // namespace planning
}  // namespace apollo
