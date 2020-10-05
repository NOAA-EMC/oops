/*
 * (C) Copyright 2009-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#ifndef TEST_INTERFACE_INCREMENT_H_
#define TEST_INTERFACE_INCREMENT_H_

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define ECKIT_TESTING_SELF_REGISTER_CASES 0

#include <boost/noncopyable.hpp>

#include "eckit/config/LocalConfiguration.h"
#include "eckit/testing/Test.h"
#include "oops/base/Variables.h"
#include "oops/interface/Geometry.h"
#include "oops/interface/Increment.h"
#include "oops/interface/State.h"
#include "oops/mpi/mpi.h"
#include "oops/runs/Test.h"
#include "oops/util/DateTime.h"
#include "oops/util/dot_product.h"
#include "oops/util/Logger.h"
#include "test/TestEnvironment.h"


namespace test {

// =============================================================================

template <typename MODEL> class IncrementFixture : private boost::noncopyable {
  typedef oops::Geometry<MODEL>       Geometry_;

 public:
  static const Geometry_            & resol()     {return *getInstance().resol_;}
  static const oops::Variables      & ctlvars()   {return *getInstance().ctlvars_;}
  static const util::DateTime       & time()      {return *getInstance().time_;}
  static const double               & tolerance() {return getInstance().tolerance_;}
  static const eckit::Configuration & test()      {return *getInstance().test_;}

 private:
  static IncrementFixture<MODEL>& getInstance() {
    static IncrementFixture<MODEL> theIncrementFixture;
    return theIncrementFixture;
  }

  IncrementFixture<MODEL>() {
//  Setup a geometry
    const eckit::LocalConfiguration resolConfig(TestEnvironment::config(), "geometry");
    resol_.reset(new Geometry_(resolConfig, oops::mpi::world()));

    ctlvars_.reset(new oops::Variables(TestEnvironment::config(), "inc variables"));

    test_.reset(new eckit::LocalConfiguration(TestEnvironment::config()));
    time_.reset(new util::DateTime(test_->getString("increment test.date")));
    const double tol_default = 1e-8;
    tolerance_ = test_->getDouble("increment test.tolerance", tol_default);
    if (tolerance_ > tol_default) {
      oops::Log::warning() <<
        "Warning: Increment norm tolerance greater than 1e-8 "
        "may not be suitable for certain solvers." <<
        std::endl; }
  }

  ~IncrementFixture<MODEL>() {}

  std::unique_ptr<Geometry_>       resol_;
  std::unique_ptr<oops::Variables> ctlvars_;
  std::unique_ptr<const eckit::LocalConfiguration> test_;
  double                           tolerance_;
  std::unique_ptr<util::DateTime>  time_;
};

// =============================================================================

template <typename MODEL> void testIncrementConstructor() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx(Test_::resol(), Test_::ctlvars(), Test_::time());

  EXPECT(dx.norm() == 0.0);
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementCopyConstructor() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();
  EXPECT(dx1.norm() > 0.0);

  Increment_ dx2(dx1);
  EXPECT(dx2.norm() > 0.0);

// Check that the copy is equal to the original
  dx2 -= dx1;
  EXPECT(dx2.norm() == 0.0);
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementTriangle() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();
  Increment_ dx2(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx2.random();

// test triangle inequality
  double dot1 = dx1.norm();
  EXPECT(dot1 > 0.0);

  double dot2 = dx2.norm();
  EXPECT(dot2 > 0.0);

  dx2 += dx1;
  double dot3 = dx2.norm();
  EXPECT(dot3 > 0.0);

  EXPECT(dot3 <= dot1 + dot2);
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementOpPlusEq() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();
  Increment_ dx2(dx1);

// test *= and +=
  dx2 += dx1;
  dx1 *= 2.0;

  dx2 -= dx1;
  EXPECT(dx2.norm() < Test_::tolerance());
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementDotProduct() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();
  Increment_ dx2(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx2.random();

// test symmetry of dot product
  double zz1 = dot_product(dx1, dx2);
  double zz2 = dot_product(dx2, dx1);

  EXPECT(zz1 == zz2);
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementZero() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx.random();
  EXPECT(dx.norm() > 0.0);

// test zero
  dx->zero();
  EXPECT(dx.norm() == 0.0);
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementAxpy() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();

// test axpy
  Increment_ dx2(dx1);
  dx2.axpy(2.0, dx1);

  dx2 -= dx1;
  dx2 -= dx1;
  dx2 -= dx1;

  EXPECT(dx2.norm() < Test_::tolerance());
}

// -----------------------------------------------------------------------------

template <typename MODEL> void testIncrementSerialize() {
  typedef IncrementFixture<MODEL>   Test_;
  typedef oops::Increment<MODEL>    Increment_;

// Create two random increments
  Increment_ dx1(Test_::resol(), Test_::ctlvars(), Test_::time());
  dx1.random();

  util::DateTime tt(Test_::time() + util::Duration("PT15H"));
  Increment_ dx2(Test_::resol(), Test_::ctlvars(), tt);

// Test serialize-deserialize
  std::vector<double> vect;
  dx1.serialize(vect);
  EXPECT(vect.size() == dx1.serialSize());

  size_t index = 0;
  dx2.deserialize(vect, index);
  EXPECT(index == dx1.serialSize());
  EXPECT(index == dx2.serialSize());

  dx1.serialize(vect);
  EXPECT(vect.size() == dx1.serialSize() * 2);

  if (dx1.serialSize() > 0) {  // until all models have implemented serialize
    EXPECT(dx1.norm() > 0.0);
    EXPECT(dx2.norm() > 0.0);
    EXPECT(dx2.validTime() == Test_::time());

    dx2 -= dx1;
    EXPECT(dx2.norm() == 0.0);
  }
}

// =============================================================================

template <typename MODEL>
class Increment : public oops::Test {
 public:
  Increment() {}
  virtual ~Increment() {}

 private:
  std::string testid() const override {return "test::Increment<" + MODEL::name() + ">";}

  void register_tests() const override {
    std::vector<eckit::testing::Test>& ts = eckit::testing::specification();

    ts.emplace_back(CASE("interface/Increment/testIncrementConstructor")
      { testIncrementConstructor<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementCopyConstructor")
      { testIncrementCopyConstructor<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementTriangle")
      { testIncrementTriangle<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementOpPlusEq")
      { testIncrementOpPlusEq<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementDotProduct")
      { testIncrementDotProduct<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementAxpy")
      { testIncrementAxpy<MODEL>(); });
    ts.emplace_back(CASE("interface/Increment/testIncrementSerialize")
      { testIncrementSerialize<MODEL>(); });
  }

  void clear() const override {}
};

// =============================================================================

}  // namespace test

#endif  // TEST_INTERFACE_INCREMENT_H_
