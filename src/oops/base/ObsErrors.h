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

#include <string>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "oops/base/Departures.h"
#include "oops/base/ObsErrorBase.h"
#include "oops/base/Observations.h"
#include "oops/base/ObsSpaces.h"
#include "oops/interface/ObsVector.h"
#include "oops/util/Logger.h"
#include "oops/util/Printable.h"

namespace oops {

// -----------------------------------------------------------------------------

template <typename MODEL>
class ObsErrors : public util::Printable,
                  private boost::noncopyable {
  typedef Departures<MODEL>          Departures_;
  typedef Observations<MODEL>        Observations_;
  typedef ObsErrorBase<MODEL>        ObsError_;
  typedef ObsSpaces<MODEL>           ObsSpaces_;
  typedef ObsVector<MODEL>           ObsVector_;

 public:
  static const std::string classname() {return "oops::ObsErrors";}

  ObsErrors(const eckit::Configuration &, const ObsSpaces_ &);
  ~ObsErrors();

/// Access
  std::size_t size() const {return err_.size();}
  const ObsError_ & operator[](const std::size_t ii) const {return *err_.at(ii);}

/// Multiply a Departure by \f$R\f$ and \f$R^{-1}\f$
  void multiply(Departures_ &) const;
  void inverseMultiply(Departures_ &) const;

/// Generate random perturbation
  void randomize(Departures_ &) const;

 private:
  void print(std::ostream &) const;
  std::vector<boost::shared_ptr<ObsError_> > err_;
};

// -----------------------------------------------------------------------------

template <typename MODEL>
ObsErrors<MODEL>::ObsErrors(const eckit::Configuration & config,
                            const ObsSpaces_ & os) : err_(0)
{
  std::vector<eckit::LocalConfiguration> obsconf;
  config.get("ObsTypes", obsconf);
  for (std::size_t jj = 0; jj < os.size(); ++jj) {
    eckit::LocalConfiguration conf(obsconf[jj], "Covariance");
    boost::shared_ptr<ObsError_> tmp(
      ObsErrorFactory<MODEL>::create(conf, os[jj]));
    err_.push_back(tmp);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
ObsErrors<MODEL>::~ObsErrors() {}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::multiply(Departures_ & dy) const {
  for (std::size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->multiply(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::inverseMultiply(Departures_ & dy) const {
  for (std::size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->inverseMultiply(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template <typename MODEL>
void ObsErrors<MODEL>::randomize(Departures_ & dy) const {
  for (std::size_t jj = 0; jj < err_.size(); ++jj) {
    err_[jj]->randomize(dy[jj]);
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ObsErrors<MODEL>::print(std::ostream & os) const {
  for (std::size_t jj = 0; jj < err_.size(); ++jj) os << *err_[jj];
}

// -----------------------------------------------------------------------------

}  // namespace oops

#endif  // OOPS_BASE_OBSERRORS_H_
