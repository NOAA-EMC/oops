/*
 * (C) Copyright 2009-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#ifndef OOPS_GENERIC_ERRORCOVARIANCEBUMP_H_
#define OOPS_GENERIC_ERRORCOVARIANCEBUMP_H_

#include <string>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "oops/base/ModelSpaceCovarianceBase.h"
#include "oops/base/Variables.h"
#include "oops/generic/oobump_f.h"
#include "oops/generic/UnstructuredGrid.h"
#include "oops/interface/Geometry.h"
#include "oops/interface/Increment.h"
#include "oops/interface/State.h"
#include "oops/util/Logger.h"
#include "oops/util/ObjectCounter.h"
#include "oops/util/Printable.h"
#include "oops/util/Timer.h"

namespace eckit {
  class LocalConfiguration;
  class Configuration;
}

namespace oops {

// -----------------------------------------------------------------------------

/// Model space error covariance on generic unstructured grid

template <typename MODEL>
class ErrorCovarianceBUMP : public oops::ModelSpaceCovarianceBase<MODEL>,
                            public util::Printable,
                            private util::ObjectCounter<ErrorCovarianceBUMP<MODEL> >,
                            private boost::noncopyable {
  typedef Geometry<MODEL>            Geometry_;
  typedef Increment<MODEL>           Increment_;
  typedef State<MODEL>               State_;

 public:
  static const std::string classname() {return "oops::ErrorCovarianceBUMP";}

  ErrorCovarianceBUMP(const Geometry_ &, const Variables &,
                      const eckit::Configuration &, const State_ &, const State_ &);
  virtual ~ErrorCovarianceBUMP();

  void randomize(Increment_ &) const;

 private:
  void doRandomize(Increment_ &) const override;
  void doMultiply(const Increment_ &, Increment_ &) const override;
  void doInverseMultiply(const Increment_ &, Increment_ &) const override;

  void print(std::ostream &) const override;

  const Variables vars_;
  int colocated_;
  int keyBUMP_;
};

// =============================================================================

template<typename MODEL>
ErrorCovarianceBUMP<MODEL>::ErrorCovarianceBUMP(const Geometry_ & resol,
                                                const Variables & vars,
                                                const eckit::Configuration & conf,
                                                const State_ & xb, const State_ & fg)
  : ModelSpaceCovarianceBase<MODEL>(xb, fg, resol, conf), vars_(vars), colocated_(1), keyBUMP_(0)
{
  Log::trace() << "ErrorCovarianceBUMP::ErrorCovarianceBUMP starting" << std::endl;
  const eckit::Configuration * fconf = &conf;

// Setup dummy increment
  Increment_ dx(resol, vars_, fg.validTime());

// Define unstructured grid coordinates
  UnstructuredGrid ug;
  dx.ug_coord(ug, colocated_);

// Delete BUMP if present
  if (keyBUMP_) delete_oobump_f90(keyBUMP_);

// Create BUMP
  create_oobump_f90(keyBUMP_, ug.toFortran(), &fconf, 0, 1, 0, 1);

// Read data from files
  if (conf.has("input")) {
    std::vector<eckit::LocalConfiguration> inputConfigs;
    conf.get("input", inputConfigs);
    for (const auto & subconf : inputConfigs) {
      dx.read(subconf);
      dx.field_to_ug(ug, colocated_);
      std::string param = subconf.getString("parameter");
      const int nstr = param.size();
      const char *cstr = param.c_str();
      set_oobump_param_f90(keyBUMP_, nstr, cstr, ug.toFortran());
    }
  }

// Run BUMP
  run_oobump_drivers_f90(keyBUMP_);

// Copy test
  std::ifstream infile("bump.test");
  std::string line;
  while (std::getline(infile, line)) Log::test() << line << std::endl;
  remove("bump.test");

  Log::trace() << "ErrorCovarianceBUMP::ErrorCovarianceBUMP done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
ErrorCovarianceBUMP<MODEL>::~ErrorCovarianceBUMP() {
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::~ErrorCovarianceBUMP starting" << std::endl;
  util::Timer timer(classname(), "~ErrorCovarianceBUMP");
  delete_oobump_f90(keyBUMP_);
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::~ErrorCovarianceBUMP done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ErrorCovarianceBUMP<MODEL>::doMultiply(const Increment_ & dx1,
                                            Increment_ & dx2) const {
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doMultiply starting" << std::endl;
  util::Timer timer(classname(), "doMultiply");
  UnstructuredGrid ug;
  dx1.field_to_ug(ug, colocated_);
  multiply_oobump_nicas_f90(keyBUMP_, ug.toFortran());
  dx2.field_from_ug(ug);
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doMultiply done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ErrorCovarianceBUMP<MODEL>::doInverseMultiply(const Increment_ & dx1,
                                                   Increment_ & dx2) const {
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doInverseMultiply starting" << std::endl;
  util::Timer timer(classname(), "doInverseMultiply");
  dx2.zero();
  Log::info() << "ErrorCovarianceBUMP<MODEL>::doInverseMultiply not implemented" << std::endl;
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doInverseMultiply done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ErrorCovarianceBUMP<MODEL>::doRandomize(Increment_ & dx) const {
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doRandomize starting" << std::endl;
  util::Timer timer(classname(), "doRandomize");
  dx.random();
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::doRandomize done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void ErrorCovarianceBUMP<MODEL>::print(std::ostream & os) const {
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::print starting" << std::endl;
  util::Timer timer(classname(), "print");
  os << "ErrorCovarianceBUMP<MODEL>::print not implemented";
  Log::trace() << "ErrorCovarianceBUMP<MODEL>::print done" << std::endl;
}

// -----------------------------------------------------------------------------

}  // namespace oops

#endif  // OOPS_GENERIC_ERRORCOVARIANCEBUMP_H_
