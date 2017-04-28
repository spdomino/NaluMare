/*------------------------------------------------------------------------*/
/*  Copyright 2014 National Renewable Energy Laboratory.                  */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include "kernels/UnitTestKernelUtils.h"
#include "UnitTestUtils.h"

#include "ContinuityAdvElemKernel.h"

namespace {
namespace hex8_golds {
namespace advection_default {

static constexpr double rhs[8] = {
  3.84734138744e-14,  6.12323399574e-16,
  -3.84734138744e-14, -6.12323399574e-16,
  3.84734138744e-14,  6.12323399574e-16,
  -3.84734138744e-14, -6.12323399574e-16,
};

static constexpr double lhs[8][8] = {
  {0.421875, -0.046875, -0.078125, -0.046875, -0.046875, -0.078125, -0.046875, -0.078125},
  {-0.046875, 0.421875, -0.046875, -0.078125, -0.078125, -0.046875, -0.078125, -0.046875},
  {-0.078125, -0.046875, 0.421875, -0.046875, -0.046875, -0.078125, -0.046875, -0.078125},
  {-0.046875, -0.078125, -0.046875, 0.421875, -0.078125, -0.046875, -0.078125, -0.046875},
  {-0.046875, -0.078125, -0.046875, -0.078125, 0.421875, -0.046875, -0.078125, -0.046875},
  {-0.078125, -0.046875, -0.078125, -0.046875, -0.046875, 0.421875, -0.046875, -0.078125},
  {-0.046875, -0.078125, -0.046875, -0.078125, -0.078125, -0.046875, 0.421875, -0.046875},
  {-0.078125, -0.046875, -0.078125, -0.046875, -0.046875, -0.078125, -0.046875, 0.421875},
};
} // advection_default

namespace advection_reduced_sensitivities
{
static constexpr double rhs[8] = {
  3.84734138744e-14, 6.12323399574e-16,
  -3.84734138744e-14, -6.12323399574e-16,
  3.84734138744e-14, 6.12323399574e-16,
  -3.84734138744e-14, -6.12323399574e-16, };

static constexpr double lhs[8][8] = {
  { 0.75, -0.25, 0, -0.25, -0.25, 0, 0, 0,  },
  { -0.25, 0.75, -0.25, 0, 0, -0.25, 0, 0,  },
  { 0, -0.25, 0.75, -0.25, 0, 0, -0.25, 0,  },
  { -0.25, 0, -0.25, 0.75, 0, 0, 0, -0.25,  },
  { -0.25, 0, 0, 0, 0.75, -0.25, 0, -0.25,  },
  { 0, -0.25, 0, 0, -0.25, 0.75, -0.25, 0,  },
  { 0, 0, -0.25, 0, 0, -0.25, 0.75, -0.25,  },
  { 0, 0, 0, -0.25, -0.25, 0, -0.25, 0.75,  },
};
} // advection_reduced_sensitivities
} // hex8_golds
} // anonymous namespace

/// Continuity advection with default Solution options
TEST_F(ContinuityKernelHex8Mesh, advection_default)
{
  fill_mesh_and_init_fields();

  // Setup solution options for default advection kernel
  solnOpts_.meshMotion_ = false;
  solnOpts_.meshDeformation_ = false;
  solnOpts_.externalMeshDeformation_ = false;
  solnOpts_.cvfemShiftMdot_ = false;
  solnOpts_.cvfemShiftPoisson_ = false;
  solnOpts_.cvfemReducedSensPoisson_ = false;
  solnOpts_.mdotInterpRhoUTogether_ = true;

  // Initialize the kernel driver
  unit_test_kernel_utils::TestKernelDriver assembleKernels(
    bulk_, partVec_, coordinates_, 1, stk::topology::HEX_8);

  // Initialize the kernel
  std::unique_ptr<sierra::nalu::Kernel> advKernel(
    new sierra::nalu::ContinuityAdvElemKernel<sierra::nalu::AlgTraitsHex8>(
      bulk_, solnOpts_, assembleKernels.dataNeededByKernels_));

  // Register the kernel for execution
  assembleKernels.activeKernels_.push_back(advKernel.get());

  // Populate LHS and RHS
  assembleKernels.execute();

  EXPECT_EQ(assembleKernels.lhs_.dimension(0), 8u);
  EXPECT_EQ(assembleKernels.lhs_.dimension(1), 8u);
  EXPECT_EQ(assembleKernels.rhs_.dimension(0), 8u);

  namespace gold_values = hex8_golds::advection_default;

  unit_test_kernel_utils::expect_all_near(assembleKernels.rhs_, gold_values::rhs);
  unit_test_kernel_utils::expect_all_near<8>(assembleKernels.lhs_, gold_values::lhs);
}

/// Continuity advection kernel
///
/// `reduced_sens_cvfem_poisson: true`
///
TEST_F(ContinuityKernelHex8Mesh, advection_reduced_sens_cvfem_poisson)
{
  fill_mesh_and_init_fields();

  // Setup solution options for default advection kernel
  solnOpts_.meshMotion_ = false;
  solnOpts_.meshDeformation_ = false;
  solnOpts_.externalMeshDeformation_ = false;
  solnOpts_.cvfemShiftMdot_ = false;
  solnOpts_.cvfemShiftPoisson_ = false;
  solnOpts_.cvfemReducedSensPoisson_ = true;
  solnOpts_.mdotInterpRhoUTogether_ = true;

  // Initialize the kernel driver
  unit_test_kernel_utils::TestKernelDriver assembleKernels(
    bulk_, partVec_, coordinates_, 1, stk::topology::HEX_8);

  // Initialize the kernel
  std::unique_ptr<sierra::nalu::Kernel> advKernel(
    new sierra::nalu::ContinuityAdvElemKernel<sierra::nalu::AlgTraitsHex8>(
      bulk_, solnOpts_, assembleKernels.dataNeededByKernels_));

  // Register the kernel for execution
  assembleKernels.activeKernels_.push_back(advKernel.get());

  // Populate LHS and RHS
  assembleKernels.execute();

  EXPECT_EQ(assembleKernels.lhs_.dimension(0), 8u);
  EXPECT_EQ(assembleKernels.lhs_.dimension(1), 8u);
  EXPECT_EQ(assembleKernels.rhs_.dimension(0), 8u);

  namespace gold_values = hex8_golds::advection_reduced_sensitivities;

  unit_test_kernel_utils::expect_all_near(assembleKernels.rhs_, gold_values::rhs);
  unit_test_kernel_utils::expect_all_near<8>(assembleKernels.lhs_, gold_values::lhs);
}