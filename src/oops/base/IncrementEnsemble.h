/*
 * (C) Copyright 2009-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#ifndef OOPS_BASE_INCREMENTENSEMBLE_H_
#define OOPS_BASE_INCREMENTENSEMBLE_H_

#include <Eigen/Dense>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "eckit/config/LocalConfiguration.h"
#include "oops/base/Geometry.h"
#include "oops/base/Increment.h"
#include "oops/base/LocalIncrement.h"
#include "oops/base/State.h"
#include "oops/base/StateEnsemble.h"
#include "oops/base/Variables.h"
#include "oops/interface/GeometryIterator.h"
#include "oops/interface/LinearVariableChange.h"
#include "oops/util/DateTime.h"
#include "oops/util/Logger.h"
#include "oops/util/parameters/OptionalParameter.h"
#include "oops/util/parameters/Parameter.h"
#include "oops/util/parameters/Parameters.h"

namespace oops {

// -----------------------------------------------------------------------------
/// Parameters for the ensemble of increments generated from ensemble of states
/// with specified inflation and linear variable changes.
template <typename MODEL>
class IncrementEnsembleFromStatesParameters : public Parameters {
  OOPS_CONCRETE_PARAMETERS(IncrementEnsembleFromStatesParameters, Parameters)

  typedef typename Increment<MODEL>::ReadParameters_ IncrementReadParameters_;
  typedef typename LinearVariableChange<MODEL>::Parameters_ LinearVarChangeParameters_;
 public:
  OptionalParameter<IncrementReadParameters_> inflationField{"inflation field",
                   "inflation field (as increment in model space)", this};
  Parameter<double> inflationValue{"inflation value", "inflation value (scalar)",
                    1.0, this};
  OptionalParameter<LinearVarChangeParameters_> linVarChange{"linear variable change",
                   "linear variable changes applied to the increments", this};
  StateEnsembleParameters<MODEL> states{this};
};


/// \brief Ensemble of increments
template<typename MODEL> class IncrementEnsemble {
  typedef Geometry<MODEL>              Geometry_;
  typedef GeometryIterator<MODEL>    GeometryIterator_;
  typedef Increment<MODEL>             Increment_;
  typedef LinearVariableChange<MODEL>  LinearVariableChange_;
  typedef State<MODEL>                 State_;
  typedef StateEnsemble<MODEL>         StateEnsemble_;
  typedef IncrementEnsembleFromStatesParameters<MODEL> IncrementEnsembleFromStatesParameters_;

 public:
  /// Constructor
  IncrementEnsemble(const Geometry_ & resol, const Variables & vars,
                    const util::DateTime &, const int rank);
  IncrementEnsemble(const IncrementEnsembleFromStatesParameters_ &, const State_ &, const State_ &,
                    const Geometry_ &, const Variables &);
  /// \brief construct ensemble of perturbations by reading them from disk
  IncrementEnsemble(const Geometry_ &, const Variables &, const eckit::Configuration &);
  /// \brief construct ensemble of perturbations by reading two state ensembles (one member at a
  //         time) and taking the  difference of each set of pairs
  IncrementEnsemble(const Geometry_ &, const Variables &, const eckit::Configuration &,
                    const eckit::Configuration &);

  void write(const eckit::Configuration &) const;

  /// Accessors
  size_t size() const {return ensemblePerturbs_.size();}
  Increment_ & operator[](const int ii) {return ensemblePerturbs_[ii];}
  const Increment_ & operator[](const int ii) const {return ensemblePerturbs_[ii];}

  /// Control variables
  const Variables & controlVariables() const {return vars_;}

  void packEigen(Eigen::MatrixXd & X, const GeometryIterator_ & gi) const;
  void setEigen(const Eigen::MatrixXd & X, const GeometryIterator_ & gi);

 private:
  const Variables vars_;
  std::vector<Increment_> ensemblePerturbs_;
};

// ====================================================================================

template<typename MODEL>
IncrementEnsemble<MODEL>::IncrementEnsemble(const Geometry_ & resol, const Variables & vars,
                                            const util::DateTime & tslot, const int rank)
  : vars_(vars), ensemblePerturbs_()
{
  ensemblePerturbs_.reserve(rank);
  for (int m = 0; m < rank; ++m) {
    ensemblePerturbs_.emplace_back(resol, vars_, tslot);
  }
  Log::trace() << "IncrementEnsemble:contructor done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
IncrementEnsemble<MODEL>::IncrementEnsemble(const IncrementEnsembleFromStatesParameters_ & params,
                                            const State_ & xb, const State_ & fg,
                                            const Geometry_ & resol, const Variables & vars)
  : vars_(vars), ensemblePerturbs_()
{
  Log::trace() << "IncrementEnsemble:contructor start" << std::endl;
  // Check sizes and fill in timeslots
  util::DateTime tslot = xb.validTime();

  // Read inflation field
  std::unique_ptr<Increment_> inflationField;
  if (params.inflationField.value() != boost::none) {
    inflationField.reset(new Increment_(resol, vars, tslot));
    inflationField->read(*params.inflationField.value());
  }

  // Get inflation value
  const double inflationValue = params.inflationValue;

  // Setup change of variable
  std::unique_ptr<LinearVariableChange_> linvarchg;
  if (params.linVarChange.value() != boost::none) {
    linvarchg.reset(new LinearVariableChange_(resol, *params.linVarChange.value()));
    linvarchg->setTrajectory(xb, fg);
  }
  // TODO(Benjamin): one change of variable for each timeslot

  // Read ensemble
  StateEnsemble_ ensemble(resol, params.states);
  State_ bgmean = ensemble.mean();

  ensemblePerturbs_.reserve(ensemble.size());
  for (unsigned int ie = 0; ie < ensemble.size(); ++ie) {
    // Ensemble will be centered around ensemble mean
    Increment_ dx(resol, vars_, tslot);
    dx.diff(ensemble[ie], bgmean);

    // Apply inflation
    if (params.inflationField.value() != boost::none) {
      dx.schur_product_with(*inflationField);
    }
    dx *= inflationValue;

    if (params.linVarChange.value() != boost::none) {
      const auto & linvar = *params.linVarChange.value();
      oops::Variables varin = *linvar.inputVariables.value();
      linvarchg->multiplyInverse(dx, varin);
    }

    ensemblePerturbs_.emplace_back(std::move(dx));
  }
  Log::trace() << "IncrementEnsemble:contructor done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
IncrementEnsemble<MODEL>::IncrementEnsemble(const Geometry_ & resol, const Variables & vars,
                                            const eckit::Configuration & config)
  : vars_(vars), ensemblePerturbs_()
{
  std::vector<eckit::LocalConfiguration> memberConfig;
  config.get("members", memberConfig);

  // Datetime for ensemble
  util::DateTime tslot = util::DateTime(config.getString("date"));

  // Reserve memory to hold ensemble
  ensemblePerturbs_.reserve(memberConfig.size());

  // Loop over all ensemble members
  for (size_t jj = 0; jj < memberConfig.size(); ++jj) {
    Increment_ dx(resol, vars_, tslot);
    dx.read(memberConfig[jj]);
    ensemblePerturbs_.emplace_back(std::move(dx));
  }
  Log::trace() << "IncrementEnsemble:contructor (by reading increment ensemble) done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
IncrementEnsemble<MODEL>::IncrementEnsemble(const Geometry_ & resol, const Variables & vars,
                                            const eckit::Configuration & configBase,
                                            const eckit::Configuration & configPert)
  : vars_(vars), ensemblePerturbs_()
{
  std::vector<eckit::LocalConfiguration> memberConfigBase;
  configBase.get("members", memberConfigBase);

  std::vector<eckit::LocalConfiguration> memberConfigPert;
  configPert.get("members", memberConfigPert);

  // Ensure input ensembles are of the same size
  ASSERT(memberConfigBase.size() == memberConfigPert.size());

  // Reserve memory to hold ensemble
  ensemblePerturbs_.reserve(memberConfigBase.size());

  // Loop over all ensemble members
  for (size_t jj = 0; jj < memberConfigBase.size(); ++jj) {
    State_ xBase(resol, memberConfigBase[jj]);
    State_ xPert(resol, memberConfigPert[jj]);
    Increment_ dx(resol, vars_, xBase.validTime());
    dx.diff(xBase, xPert);
    ensemblePerturbs_.emplace_back(std::move(dx));
  }
  Log::trace() << "IncrementEnsemble:contructor (by diffing state ensembles) done" << std::endl;
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void IncrementEnsemble<MODEL>::write(const eckit::Configuration & config) const
{
  eckit::LocalConfiguration outConfig(config);
  for (size_t ii=0; ii < size(); ++ii) {
    outConfig.set("member", ii+1);
    ensemblePerturbs_[ii].write(outConfig);
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void IncrementEnsemble<MODEL>::packEigen(Eigen::MatrixXd & X,
                                         const GeometryIterator_ & gi) const
{
  size_t ngp = ensemblePerturbs_[0].getLocal(gi).getVals().size();
  size_t nens = ensemblePerturbs_.size();
  X.resize(ngp, nens);
  for (size_t iens=0; iens < nens; ++iens) {
    LocalIncrement gp = ensemblePerturbs_[iens].getLocal(gi);
    std::vector<double> tmp1 = gp.getVals();
    for (size_t iv=0; iv < ngp; ++iv) {
      X(iv, iens) = tmp1[iv];
    }
  }
}

// -----------------------------------------------------------------------------

template<typename MODEL>
void IncrementEnsemble<MODEL>::setEigen(const Eigen::MatrixXd & X,
                                        const GeometryIterator_ & gi)
{
  size_t ngp = ensemblePerturbs_[0].getLocal(gi).getVals().size();
  size_t nens = ensemblePerturbs_.size();

  LocalIncrement gptmp = ensemblePerturbs_[0].getLocal(gi);
  std::vector<double> tmp = gptmp.getVals();

  for (size_t iens=0; iens < nens; ++iens) {
    for (size_t iv=0; iv < ngp; ++iv) {
      tmp[iv] = X(iv, iens);
    }
    gptmp.setVals(tmp);
    ensemblePerturbs_[iens].setLocal(gptmp, gi);
  }
}

// -----------------------------------------------------------------------------

}  // namespace oops

#endif  // OOPS_BASE_INCREMENTENSEMBLE_H_
