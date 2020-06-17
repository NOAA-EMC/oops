/*
 * (C) Copyright 2009-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#ifndef OOPS_BASE_OBSERRORBASE_H_
#define OOPS_BASE_OBSERRORBASE_H_

#include <map>
#include <memory>
#include <string>

#include <boost/noncopyable.hpp>
#include "eckit/config/Configuration.h"

#include "oops/interface/ObsSpace.h"
#include "oops/util/abor1_cpp.h"
#include "oops/util/Logger.h"
#include "oops/util/Printable.h"

namespace oops {

// -----------------------------------------------------------------------------
/// \brief Base class for observation error covariance matrices.
template<typename MODEL>
class ObsErrorBase : public util::Printable,
                     private boost::noncopyable {
  typedef ObsVector<MODEL>        ObsVector_;
  typedef ObsSpace<MODEL>         ObsSpace_;

 public:
  ObsErrorBase() = default;
  virtual ~ObsErrorBase() = default;

/// Multiply a Departure \p dy by \f$R\f$$
  virtual void multiply(ObsVector_ & dy) const = 0;
/// Multiply a Departure \p dy by \f$R^{-1}\f$
  virtual void inverseMultiply(ObsVector_ & dy) const = 0;

/// Generate random perturbation in \p dy
  virtual void randomize(ObsVector_ & dy) const = 0;

/// Return inverseVariance
  virtual const ObsVector_ & inverseVariance() const = 0;

/// Get mean error for Jo table
  virtual double getRMSE() const = 0;
};

// =============================================================================

/// ObsErrorFactory Factory
template <typename MODEL>
class ObsErrorFactory {
  typedef ObsSpace<MODEL> ObsSpace_;
 public:
  static std::unique_ptr<ObsErrorBase<MODEL> > create(const eckit::Configuration &,
                                                      const ObsSpace_ &);
  virtual ~ObsErrorFactory() = default;
 protected:
  explicit ObsErrorFactory(const std::string &);
 private:
  virtual ObsErrorBase<MODEL> * make(const eckit::Configuration &, const ObsSpace_ &) = 0;
  static std::map < std::string, ObsErrorFactory<MODEL> * > & getMakers() {
    static std::map < std::string, ObsErrorFactory<MODEL> * > makers_;
    return makers_;
  }
};

// -----------------------------------------------------------------------------

template<class MODEL, class T>
class ObsErrorMaker : public ObsErrorFactory<MODEL> {
  typedef ObsSpace<MODEL> ObsSpace_;
  virtual ObsErrorBase<MODEL> * make(const eckit::Configuration & conf,
                                     const ObsSpace_ & obs)
    { return new T(conf, obs); }
 public:
  explicit ObsErrorMaker(const std::string & name) : ObsErrorFactory<MODEL>(name) {}
};

// =============================================================================

template <typename MODEL>
ObsErrorFactory<MODEL>::ObsErrorFactory(const std::string & name) {
  if (getMakers().find(name) != getMakers().end()) {
    Log::error() << name << " already registered in observation error factory." << std::endl;
    ABORT("Element already registered in ObsErrorFactory.");
  }
  getMakers()[name] = this;
}

// -----------------------------------------------------------------------------

template <typename MODEL>
std::unique_ptr<ObsErrorBase<MODEL>>
ObsErrorFactory<MODEL>::create(const eckit::Configuration & conf, const ObsSpace_ & obs) {
  Log::trace() << "ObsErrorBase<MODEL>::create starting" << std::endl;
  const std::string id = conf.getString("covariance");
  typename std::map<std::string, ObsErrorFactory<MODEL>*>::iterator
    jerr = getMakers().find(id);
  if (jerr == getMakers().end()) {
    Log::error() << id << " does not exist in observation error factory." << std::endl;
    ABORT("Element does not exist in ObsErrorFactory.");
  }
  std::unique_ptr<ObsErrorBase<MODEL>> ptr(jerr->second->make(conf, obs));
  Log::trace() << "ObsErrorBase<MODEL>::create done" << std::endl;
  return ptr;
}

// -----------------------------------------------------------------------------

}  // namespace oops

#endif  // OOPS_BASE_OBSERRORBASE_H_
