/*
 * (C) Copyright 2022-2022 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include "lorenz95/InterpolatorL95.h"

#include <ostream>
#include <vector>

#include "lorenz95/IncrementL95.h"
#include "lorenz95/Resolution.h"
#include "lorenz95/StateL95.h"

namespace lorenz95 {

// -----------------------------------------------------------------------------

InterpolatorL95::InterpolatorL95(const eckit::Configuration &,
                                 const Resolution & resol, const std::vector<double> & locs)
  : nout_(0), ilocs_(nout_)
{
  ASSERT(locs.size() % 2 == 0);
  nout_ = locs.size() / 2;
  const size_t res = resol.npoints();
  const double dres = static_cast<double>(res);
  ilocs_.resize(nout_);
  for (size_t jj = 0; jj < nout_; ++jj) {
    int ii = round(locs[2 * jj + 1] * dres);
    ASSERT(ii >= 0 && ii <= res);
    if (ii == res) ii = 0;
    ilocs_[jj] = ii;
  }
}

// -----------------------------------------------------------------------------

InterpolatorL95::~InterpolatorL95() {}

// -----------------------------------------------------------------------------

void InterpolatorL95::apply(const oops::Variables &, const StateL95 & xx,
                            std::vector<double> & vals) const {
  vals.resize(nout_);
  for (size_t jj = 0; jj < nout_; ++jj) {
    vals[jj] = xx.getField()[ilocs_[jj]];
  }
}

// -----------------------------------------------------------------------------

void InterpolatorL95::apply(const oops::Variables &, const IncrementL95 & dx,
                            std::vector<double> & vals) const {
  vals.resize(nout_);
  for (size_t jj = 0; jj < nout_; ++jj) {
    vals[jj] = dx.getField()[ilocs_[jj]];
  }
}

// -----------------------------------------------------------------------------

void InterpolatorL95::applyAD(const oops::Variables &, IncrementL95 & dx,
                              const std::vector<double> & vals) const {
  ASSERT(vals.size() == nout_);
  for (size_t jj = 0; jj < nout_; ++jj) {
    dx.getField()[ilocs_[jj]] += vals[jj];
  }
}

// -----------------------------------------------------------------------------

void InterpolatorL95::print(std::ostream & os) const {
  os << "InterpolatorL95";
}


// -----------------------------------------------------------------------------

}  // namespace lorenz95
