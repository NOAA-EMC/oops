/*
 * (C) Copyright 2022-2022 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "atlas/array.h"
#include "atlas/field.h"
#include "atlas/util/Geometry.h"
#include "atlas/util/KDTree.h"
#include "atlas/util/Metadata.h"
#include "atlas/util/Point.h"
#include "eckit/config/Configuration.h"
#include "eckit/exception/Exceptions.h"

#include "oops/base/Geometry.h"
#include "oops/base/GeometryData.h"
#include "oops/base/Increment.h"
#include "oops/base/State.h"
#include "oops/base/Variables.h"
#include "oops/external/stripack/stripack.h"
#include "oops/util/Logger.h"
#include "oops/util/missingValues.h"
#include "oops/util/ObjectCounter.h"
#include "oops/util/Printable.h"
#include "oops/util/Timer.h"

namespace detail {
template <size_t N>
std::array<size_t, N> decreasing_permutation(const std::array<double, N> & arr) {
  std::array<std::size_t, N> p{};
  std::iota(p.begin(), p.end(), 0);
  std::sort(p.begin(), p.end(), [&arr](const size_t i, const size_t j){ return arr[i] > arr[j]; });
  return p;
}

template <typename T, size_t N>
std::array<T, N> permute_array(const std::array<T, N> & arr, const std::array<std::size_t, N> & p) {
  std::array<T, N> sorted{};
  std::transform(p.begin(), p.end(), sorted.begin(), [&](const size_t i){ return arr[i]; });
  return sorted;
}
}  // namespace detail

namespace oops {

// -----------------------------------------------------------------------------

template<typename MODEL>
class UnstructuredInterpolator : public util::Printable,
                                 private util::ObjectCounter<UnstructuredInterpolator<MODEL>> {
  typedef State<MODEL>     State_;
  typedef Geometry<MODEL>  Geometry_;
  typedef Increment<MODEL> Increment_;

 public:
  static const std::string classname() {return "oops::UnstructuredInterpolator";}

  UnstructuredInterpolator(const eckit::Configuration &, const Geometry_ &,
                           const std::vector<double> &, const std::vector<double> &);
  // Interpolator interface with no target-point mask, i.e., interpolates to every target point.
  void apply(const Variables &, const State_ &, std::vector<double> &) const;
  void apply(const Variables &, const Increment_ &, std::vector<double> &) const;
  void applyAD(const Variables &, Increment_ &, const std::vector<double> &) const;

  void apply(const Variables &, const atlas::FieldSet &, std::vector<double> &) const;
  void applyAD(const Variables &, atlas::FieldSet &, const std::vector<double> &) const;

  // Interpolator interface with a target-point mask, i.e., interpolates to target points for which
  // the mask is true. At points for which mask is false, the return vector is unmodified from its
  // input state.
  void apply(const Variables &, const State_ &, const std::vector<bool> &,
             std::vector<double> &) const;
  void apply(const Variables &, const Increment_ &, const std::vector<bool> &,
             std::vector<double> &) const;
  void applyAD(const Variables &, Increment_ &, const std::vector<bool> &,
               const std::vector<double> &) const;

  void apply(const Variables &, const atlas::FieldSet &, const std::vector<bool> &,
             std::vector<double> &) const;
  void applyAD(const Variables &, atlas::FieldSet &, const std::vector<bool> &,
               const std::vector<double> &) const;

  // Unscramble MPI buffer into the model's FieldSet representation
  // Methods are static because they do NOT rely on any internal state of the interpolator; they
  // only encode the inverse of the transformation done in apply() to get an MPI buffer from the
  // FieldSet
  static void bufferToFieldSet(const Variables &, const std::vector<size_t> &,
                               const std::vector<double> &, atlas::FieldSet &);
  static void bufferToFieldSetAD(const Variables &, const std::vector<size_t> &,
                                 std::vector<double> &, const atlas::FieldSet &);

 private:
  // Small struct to help organize the interpolation matrices (= stencils and weights)
  struct InterpMatrix {
    std::vector<bool> targetHasValidStencil;
    std::vector<std::vector<size_t>> stencils;
    std::vector<std::vector<double>> weights;
  };

  void applyPerLevel(const InterpMatrix &,
                     const std::string &,
                     const std::vector<bool> &,
                     const atlas::array::ArrayView<double, 2> &,
                     std::vector<double>::iterator &, const size_t &) const;
  void applyPerLevelAD(const InterpMatrix &,
                       const std::string &,
                       const std::vector<bool> &,
                       atlas::array::ArrayView<double, 2> &,
                       std::vector<double>::const_iterator &, const size_t &) const;
  void print(std::ostream &) const override;

  void computeUnmaskedInterpMatrix(std::vector<double>, std::vector<double>) const;
  void computeMaskedInterpMatrix(const std::string &,
                                 const atlas::array::ArrayView<double, 2> &) const;

  const GeometryData & geom_;
  size_t nout_;

  // Current triangulation-based algorithm requires the interpolation stencil to contain 3 points,
  // but this could in principle depend on the config if we generalize the algorithm to perform
  // higher-order (than linear) interpolations using information from neighboring triangles.
  constexpr static size_t nstencil_ = 3;

  // The interpolation matrices depend on the mask used at runtime. We cache the matrices as they
  // are computed, to save computations across multiple interpolations using the same mask.
  // The caching is an implementation detail, so is done using a mutable member to preserve a
  // const interpolation interface. This may break threadsafety!
  mutable std::unordered_map<std::string, InterpMatrix> interp_matrices_;
  const std::string unmaskedName_{"unmasked"};
};

// -----------------------------------------------------------------------------

template<typename MODEL>
UnstructuredInterpolator<MODEL>::UnstructuredInterpolator(const eckit::Configuration & /*config*/,
                                                          const Geometry_ & grid,
                                                          const std::vector<double> & lats_out,
                                                          const std::vector<double> & lons_out)
  : geom_(grid.generic()), nout_(0), interp_matrices_{}
{
  Log::trace() << "UnstructuredInterpolator::UnstructuredInterpolator start" << std::endl;
  util::Timer timer("oops::UnstructuredInterpolator", "UnstructuredInterpolator");

  ASSERT(lats_out.size() == lons_out.size());
  nout_ = lats_out.size();

  computeUnmaskedInterpMatrix(lats_out, lons_out);

  Log::trace() << "UnstructuredInterpolator::UnstructuredInterpolator done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const State_ & xx,
                                            std::vector<double> & locvals) const
{
  std::vector<bool> target_mask(nout_, true);
  this->apply(vars, xx.fieldSet(), target_mask, locvals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const Increment_ & dx,
                                            std::vector<double> & locvals) const
{
  std::vector<bool> target_mask(nout_, true);
  this->apply(vars, dx.fieldSet(), target_mask, locvals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyAD(const Variables & vars, Increment_ & dx,
                                              const std::vector<double> & vals) const {
  std::vector<bool> target_mask(nout_, true);
  this->applyAD(vars, dx.fieldSet(), target_mask, vals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const atlas::FieldSet & fset,
                                            std::vector<double> & locvals) const
{
  std::vector<bool> target_mask(nout_, true);
  this->apply(vars, fset, target_mask, locvals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyAD(const Variables & vars, atlas::FieldSet & fset,
                                              const std::vector<double> & vals) const {
  std::vector<bool> target_mask(nout_, true);
  this->applyAD(vars, fset, target_mask, vals);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const State_ & xx,
                                            const std::vector<bool> & target_mask,
                                            std::vector<double> & locvals) const
{
  this->apply(vars, xx.fieldSet(), target_mask, locvals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const Increment_ & dx,
                                            const std::vector<bool> & target_mask,
                                            std::vector<double> & locvals) const
{
  this->apply(vars, dx.fieldSet(), target_mask, locvals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyAD(const Variables & vars, Increment_ & dx,
                                              const std::vector<bool> & target_mask,
                                              const std::vector<double> & vals) const {
  this->applyAD(vars, dx.fieldSet(), target_mask, vals);
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::apply(const Variables & vars, const atlas::FieldSet & fset,
                                            const std::vector<bool> & target_mask,
                                            std::vector<double> & vals) const {
  Log::trace() << "UnstructuredInterpolator::apply starting" << std::endl;
  util::Timer timer("oops::UnstructuredInterpolator", "apply");

  ASSERT(target_mask.size() == nout_);

  size_t nflds = 0;
  for (size_t jf = 0; jf < vars.size(); ++jf) {
    const std::string & fname = vars[jf];
    nflds += fset.field(fname).levels();
  }
  vals.resize(nout_ * nflds);

  auto current = vals.begin();
  for (size_t jf = 0; jf < vars.size(); ++jf) {
    const std::string & fname = vars[jf];
    atlas::Field & fld = fset.field(fname);  // const in principle, but intel can't compile that

    const std::string interp_type = fld.metadata().get<std::string>("interp_type");
    ASSERT(interp_type == "default" || interp_type == "integer" || interp_type == "nearest");

    // Mask is optional -- no metadata signals unmasked interpolation
    // Warning: if the model code typoes the name of the metadata field "interp_source_point_mask",
    // then the code below will silently skip the masking and proceed with unmasked interpolation.
    // Requiring the mask metadata to be always present would increase robustness, but would require
    // all models to adapt.
    std::string maskName = unmaskedName_;
    if (fld.metadata().has("interp_source_point_mask")) {
      maskName = fld.metadata().get<std::string>("interp_source_point_mask");
      ASSERT(geom_.has(maskName));
      const atlas::Field & source_mask_fld = geom_.getField(maskName);
      ASSERT(source_mask_fld.shape(0) == fld.shape(0));
      ASSERT(source_mask_fld.shape(1) == 1);  // For now, support 2D masks only
      const auto source_mask = atlas::array::make_view<double, 2>(source_mask_fld);

      // Compute the masked interpolation matrix for this mask, if not previously done
      if (interp_matrices_.find(maskName) == interp_matrices_.end()) {
        computeMaskedInterpMatrix(maskName, source_mask);
      }
    }

    // Get interpolation matrix for this mask
    const auto & interpMatrix = interp_matrices_.at(maskName);

    const atlas::array::ArrayView<double, 2> fldin = atlas::array::make_view<double, 2>(fld);
    for (size_t jlev = 0; jlev < fldin.shape(1); ++jlev) {
      this->applyPerLevel(interpMatrix, interp_type, target_mask, fldin, current, jlev);
    }
  }
  Log::trace() << "UnstructuredInterpolator::apply done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyAD(const Variables & vars, atlas::FieldSet & fset,
                                              const std::vector<bool> & target_mask,
                                              const std::vector<double> & vals) const {
  Log::trace() << "UnstructuredInterpolator::applyAD starting" << std::endl;
  util::Timer timer("oops::UnstructuredInterpolator", "applyAD");

  ASSERT(target_mask.size() == nout_);

  std::vector<double>::const_iterator current = vals.begin();
  for (size_t jf = 0; jf < vars.size(); ++jf) {
    const std::string & fname = vars[jf];
    atlas::Field & fld = fset.field(fname);

//    const std::string interp_type = fld.metadata().get<std::string>("interp_type");
//    ASSERT(interp_type == "default" || interp_type == "integer" || interp_type == "nearest");
    const std::string interp_type = "default";

    // Mask is optional -- no metadata signals unmasked interpolation
    std::string maskName = unmaskedName_;
    if (fld.metadata().has("interp_source_point_mask")) {
      maskName = fld.metadata().get<std::string>("interp_source_point_mask");
      ASSERT(geom_.has(maskName));
      const atlas::Field & source_mask_fld = geom_.getField(maskName);
      ASSERT(source_mask_fld.shape(0) == fld.shape(0));
      ASSERT(source_mask_fld.shape(1) == 1);  // For now, support 2D masks only
      const auto source_mask = atlas::array::make_view<double, 2>(source_mask_fld);

      // Compute the masked interpolation matrix for this mask, if not previously done
      if (interp_matrices_.find(maskName) == interp_matrices_.end()) {
        computeMaskedInterpMatrix(maskName, source_mask);
      }
    }

    // Get interpolation matrix for this mask
    const auto & interpMatrix = interp_matrices_.at(maskName);

    atlas::array::ArrayView<double, 2> fldin = atlas::array::make_view<double, 2>(fld);
    for (size_t jlev = 0; jlev < fldin.shape(1); ++jlev) {
      this->applyPerLevelAD(interpMatrix, interp_type, target_mask, fldin, current, jlev);
    }
  }
  Log::trace() << "UnstructuredInterpolator::applyAD done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyPerLevel(
    const InterpMatrix & interpMatrix,
    const std::string & interp_type,
    const std::vector<bool> & target_mask,
    const atlas::array::ArrayView<double, 2> & gridin,
    std::vector<double>::iterator & gridout, const size_t & ilev) const {
  for (size_t jloc = 0; jloc < nout_; ++jloc) {
    if (target_mask[jloc]) {
      *gridout = 0.0;

      // Edge case: no valid stencil to interpolate to this target => return missingValue
      if (!interpMatrix.targetHasValidStencil[jloc]) {
        *gridout = util::missingValue(double());
        ++gridout;
        continue;
      }

      const std::vector<size_t> & interp_is = interpMatrix.stencils[jloc];
      const std::vector<double> & interp_ws = interpMatrix.weights[jloc];

      if (interp_type == "default") {
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          *gridout += interp_ws[jj] * gridin(interp_is[jj], ilev);
        }
      } else if (interp_type == "integer") {
        // Find which integer value has largest weight in the stencil. We do this by taking two
        // passes through the (usually short) data: first to identify range of values, then to
        // determine weights for each integer.
        // Note that a std::map would be shorter to code, because it would avoid needing to find
        // the range of possible integer values, but vectors are almost always much more efficient.
        int minval = std::numeric_limits<int>().max();
        int maxval = std::numeric_limits<int>().min();
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          minval = std::min(minval, static_cast<int>(std::round(gridin(interp_is[jj], ilev))));
          maxval = std::max(maxval, static_cast<int>(std::round(gridin(interp_is[jj], ilev))));
        }
        std::vector<double> int_weights(maxval - minval + 1, 0.0);
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          const int this_int = std::round(gridin(interp_is[jj], ilev));
          int_weights[this_int - minval] += interp_ws[jj];
        }
        *gridout = minval + std::distance(int_weights.begin(),
            std::max_element(int_weights.begin(), int_weights.end()));
      } else if (interp_type == "nearest") {
        // Return value from closest unmasked source point
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          if (interp_ws[jj] > 1.0e-9) {  // use a small tolerance to allow for roundoff in weights
            *gridout = gridin(interp_is[jj], ilev);
            break;
          }
        }
      } else {
        throw eckit::BadValue("Unknown interpolation type");
      }
    }
    ++gridout;
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::applyPerLevelAD(
    const InterpMatrix & interpMatrix,
    const std::string & interp_type,
    const std::vector<bool> & target_mask,
    atlas::array::ArrayView<double, 2> & gridin,
    std::vector<double>::const_iterator & gridout,
    const size_t & ilev) const {
  for (size_t jloc = 0; jloc < nout_; ++jloc) {
    if (target_mask[jloc]) {
      // (Adjoint of) No valid stencil to interpolate to this target => return missingValue
      if (!interpMatrix.targetHasValidStencil[jloc]) {
        ++gridout;
        continue;
      }

      const std::vector<size_t> & interp_is = interpMatrix.stencils[jloc];
      const std::vector<double> & interp_ws = interpMatrix.weights[jloc];

      if (interp_type == "default") {
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          gridin(interp_is[jj], ilev) += interp_ws[jj] * *gridout;
        }
      } else if (interp_type == "integer") {
        throw eckit::BadValue("No adjoint for integer interpolation");
      } else if (interp_type == "nearest") {
        // (Adjoint of) Return value from closest unmasked source point
        for (size_t jj = 0; jj < nstencil_; ++jj) {
          if (interp_ws[jj] > 1.0e-9) {  // use a small tolerance to allow for roundoff in weights
            gridin(interp_is[jj], ilev) += *gridout;
            break;
          }
        }
      } else {
        throw eckit::BadValue("Unknown interpolation type");
      }
    }
    ++gridout;
  }
}

// -----------------------------------------------------------------------------

// Unscramble MPI buffer into the model's FieldSet representation
template<typename MODEL>
void UnstructuredInterpolator<MODEL>::bufferToFieldSet(const Variables & vars,
                                                       const std::vector<size_t> & buffer_indices,
                                                       const std::vector<double> & buffer,
                                                       atlas::FieldSet & target) {
  const size_t buffer_chunk_size = buffer_indices.size();
  const size_t buffer_size = buffer.size();
  ASSERT(buffer_chunk_size > 0);
  ASSERT(buffer_size % buffer_chunk_size == 0);

  const auto buffer_start = buffer.begin();
  auto current = buffer.begin();

  for (size_t jf = 0; jf < vars.size(); ++jf) {
    const std::string & fname = vars[jf];
    atlas::Field & field = target.field(fname);

    atlas::array::ArrayView<double, 2> view = atlas::array::make_view<double, 2>(field);
    const size_t field_size = view.shape(0);
    const size_t num_levels = view.shape(1);
    ASSERT(buffer_chunk_size <= field_size);
    for (size_t jlev = 0; jlev < num_levels; ++jlev) {
      for (size_t ji = 0; ji < buffer_chunk_size; ++ji, ++current) {
        const size_t index = buffer_indices[ji];
        ASSERT(std::distance(buffer_start, current) < buffer_size);
        view(index, jlev) = *current;
      }
    }
  }
}

// -----------------------------------------------------------------------------

// (Adjoint of) Unscramble MPI buffer into the model's FieldSet representation
template<typename MODEL>
void UnstructuredInterpolator<MODEL>::bufferToFieldSetAD(const Variables & vars,
                                                         const std::vector<size_t> & buffer_indices,
                                                         std::vector<double> & buffer,
                                                         const atlas::FieldSet & target) {
  const size_t buffer_chunk_size = buffer_indices.size();
  const size_t buffer_size = buffer.size();
  ASSERT(buffer_chunk_size > 0);
  ASSERT(buffer_size % buffer_chunk_size == 0);

  const auto buffer_start = buffer.begin();
  auto current = buffer.begin();

  for (size_t jf = 0; jf < vars.size(); ++jf) {
    const std::string & fname = vars[jf];
    atlas::Field & field = target.field(fname);  // const in principle, but intel can't compile that

    const atlas::array::ArrayView<double, 2> view = atlas::array::make_view<double, 2>(field);
    const size_t field_size = view.shape(0);
    const size_t num_levels = view.shape(1);
    ASSERT(buffer_chunk_size <= field_size);
    for (size_t jlev = 0; jlev < num_levels; ++jlev) {
      for (size_t ji = 0; ji < buffer_chunk_size; ++ji, ++current) {
        const size_t index = buffer_indices[ji];
        ASSERT(std::distance(buffer_start, current) < buffer_size);
        *current += view(index, jlev);
      }
    }
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::print(std::ostream & os) const
{
  os << "UnstructuredInterpolator<" << MODEL::name() << ">";
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::computeUnmaskedInterpMatrix(
    std::vector<double> lats_out,
    std::vector<double> lons_out) const {
  // Check matrix hasn't already been set
  ASSERT(interp_matrices_.find(unmaskedName_) == interp_matrices_.end());

  // Compute interpolation matrix with no source-point mask
  interp_matrices_.insert(
      std::make_pair(unmaskedName_, InterpMatrix{
        std::vector<bool>(nout_, true),
        std::vector<std::vector<size_t>>(nout_, std::vector<size_t>(nstencil_)),
        std::vector<std::vector<double>>(nout_, std::vector<double>(nstencil_, 0.0))}));

  for (size_t jloc = 0; jloc < nout_; ++jloc) {
    std::array<int, 3> indices{};
    std::array<double, 3> baryCoords{};
    const bool validTriangle = geom_.containingTriangleAndBarycentricCoords(
        lats_out[jloc], lons_out[jloc], indices, baryCoords);

    // Edge case: target point outside of source grid, can occur for local-area models
    if (!validTriangle) {
      interp_matrices_[unmaskedName_].targetHasValidStencil[jloc] = false;
      continue;
    }

    // Reorder points from nearest to furthest (barycentric coords from largest to smallest)
    // This ordering of the coefficients is used in nearest-neighbor interpolations
    const auto permutation = detail::decreasing_permutation(baryCoords);
    indices = detail::permute_array(indices, permutation);
    baryCoords = detail::permute_array(baryCoords, permutation);

    // STRIPACK produces unnormalized barycentric coords, so normalize
    double wsum = 0.0;
    for (size_t j = 0; j < nstencil_; ++j) {
      wsum += baryCoords[j];
    }
    ASSERT(wsum > 0.0);

    // Store indices and weights into InterpMatrix datastructure
    std::vector<size_t> & interp_is = interp_matrices_.at(unmaskedName_).stencils[jloc];
    std::vector<double> & interp_ws = interp_matrices_.at(unmaskedName_).weights[jloc];
    for (size_t j = 0; j < nstencil_; ++j) {
      interp_is[j] = indices[j];
      interp_ws[j] = baryCoords[j] / wsum;
      ASSERT(interp_ws[j] >= 0.0 && interp_ws[j] <= 1.0);
    }
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void UnstructuredInterpolator<MODEL>::computeMaskedInterpMatrix(
    const std::string & maskName,
    const atlas::array::ArrayView<double, 2> & source_mask) const
{
  // Check unmasked matrix is already computed
  ASSERT(interp_matrices_.find(unmaskedName_) != interp_matrices_.end());
  // Check matrix hasn't already been computed for this mask
  ASSERT(interp_matrices_.find(maskName) == interp_matrices_.end());

  // Copy unmasked matrix, then modify it below
  interp_matrices_[maskName] = interp_matrices_[unmaskedName_];

  for (size_t jloc = 0; jloc < nout_; ++jloc) {
    // Edge case: unmasked interp stencil is already invalid => masked matrix also invalid
    if (!interp_matrices_[unmaskedName_].targetHasValidStencil[jloc]) {
      interp_matrices_[maskName].targetHasValidStencil[jloc] = false;
      continue;
    }
    std::vector<size_t> & interp_is = interp_matrices_[maskName].stencils[jloc];
    std::vector<double> & interp_ws = interp_matrices_[maskName].weights[jloc];

    // Sum up mask weights, will be used to renormalize interpolation weights
    double normalization = 0.0;
    for (size_t jj = 0; jj < nstencil_; ++jj) {
      ASSERT(source_mask(interp_is[jj], 0) >= 0.0 && source_mask(interp_is[jj], 0) <= 1.0);
      normalization += interp_ws[jj] * source_mask(interp_is[jj], 0);
    }

    if (normalization <= 1e-9) {
      // Edge case: all source points are masked out, so can't interpolate to this target point
      interp_matrices_[maskName].targetHasValidStencil[jloc] = false;
    } else {
      // Standard case: renormalize
      for (size_t jj = 0; jj < nstencil_; ++jj) {
        interp_ws[jj] *= source_mask(interp_is[jj], 0) / normalization;
      }
    }
  }
}

// -----------------------------------------------------------------------------

}  // namespace oops
