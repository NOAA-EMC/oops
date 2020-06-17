/*
 * (C) Copyright 2009-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#ifndef OOPS_BASE_OBSERRORS_H_
#define OOPS_BASE_OBSERRORS_H_

#include <Eigen/Dense>
#include <memory>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>

#include "oops/base/Departures.h"
#include "oops/base/ObsErrorBase.h"
#include "oops/base/ObsSpaces.h"
#include "oops/util/Logger.h"
#include "oops/util/Printable.h"

namespace oops {

// -----------------------------------------------------------------------------
/// \biref Container for ObsErrors for all observation types that are used in DA
template <typename MODEL>
class ObsErrors : public util::Printable,
                  private boost::noncopyable {
  typedef Departures<MODEL>          Departures_;
  typedef ObsErrorBase<MODEL>        ObsError_;
  typedef ObsSpaces<MODEL>           ObsSpaces_;
  typedef ObsVector<MODEL>           ObsVector_;

 public:
  static const std::string classname() {return "oops::ObsErrors";}

  ObsErrors(const eckit::Configuration &, const ObsSpaces_ &);

/// Accessor and size
  size_t size() const {return err_.size();}
  const ObsError_ & operator[](const size_t ii) const {return *err_.at(ii);}

/// Multiply a Departure by \f$R\f$
  void multiply(Departures_ &) const;
/// Multiply a Departure by \f$R^{-1}\f$
  void inverseMultiply(Departures_ &) const;

/// Generate random perturbation
  void randomize(Departures_ &) const;

/// Pack inverseVariance
  Eigen::MatrixXd packInverseVarianceEigen() const;

 private:
  void print(std::ostream &) const;
  std::vector<std::unique_ptr<ObsError_> > err_;
};

// -----------------------------------------------------------------------------

template <typename MODEL>
ObsErrors<MODEL>::ObsErrors(const eckit::Configuration & config,
                            const ObsSpaces_ & os) : err_() {
  std::vector<eckit::LocalConfiguration> obsconf;
  config.get("ObsTypes", obsconf);
  for (size_t jj = 0; jj < os.size(); ++jj) {
    eckit::LocalConfiguration conf(obsconf[jj], "Covariance");
    err_.emplace_back(ObsErrorFactory<MODEL>::create(conf, os[jj]));
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::multiply(Departures_ & dy) const {
  for (size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->multiply(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::inverseMultiply(Departures_ & dy) const {
  for (size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->inverseMultiply(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::randomize(Departures_ & dy) const {
  for (size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->randomize(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
Eigen::MatrixXd ObsErrors<MODEL>::packInverseVarianceEigen() const {
  // compute nobs accross all obs errors
  int nobs = 0;
  for (size_t iov = 0; iov < err_.size(); ++iov) {
    const ObsVector_ & ov = err_[iov]->inverseVariance();
    nobs += ov.nobs();
  }

  // concatinate all inverseVariance into a 1d vector
  Eigen::MatrixXd data1d(1, nobs);
  size_t i = 0;
  for (size_t iov = 0; iov < err_.size(); ++iov) {
    const ObsVector_ & ov = err_[iov]->inverseVariance();
    for (size_t iob = 0; iob < ov.nobs(); ++iob) {
      data1d(i++) = ov[iob];
    }
  }
  return data1d;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ObsErrors<MODEL>::print(std::ostream & os) const {
  for (size_t jj = 0; jj < err_.size(); ++jj) os << *err_[jj];
}

// -----------------------------------------------------------------------------

}  // namespace oops

#endif  // OOPS_BASE_OBSERRORS_H_
