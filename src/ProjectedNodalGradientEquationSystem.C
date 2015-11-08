/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <ProjectedNodalGradientEquationSystem.h>

#include <AssemblePNGElemSolverAlgorithm.h>
#include <AssemblePNGBoundarySolverAlgorithm.h>
#include <EquationSystem.h>
#include <EquationSystems.h>
#include <Enums.h>
#include <FieldFunctions.h>
#include <LinearSolvers.h>
#include <LinearSolver.h>
#include <LinearSystem.h>
#include <NaluEnv.h>
#include <Realm.h>
#include <Realms.h>
#include <Simulation.h>
#include <SolutionOptions.h>
#include <SolverAlgorithmDriver.h>

// user functions
#include <user_functions/SteadyThermalContactAuxFunction.h>

// stk_util
#include <stk_util/parallel/Parallel.hpp>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldParallel.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/CoordinateSystems.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/SkinMesh.hpp>
#include <stk_mesh/base/Comm.hpp>

// stk_io
#include <stk_io/IossBridge.hpp>

#include <stk_topology/topology.hpp>

// stk_util
#include <stk_util/parallel/ParallelReduce.hpp>
#include <stk_util/environment/CPUTime.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// ProjectedNodalGradientEquationSystem - do some stuff
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
ProjectedNodalGradientEquationSystem::ProjectedNodalGradientEquationSystem(
 EquationSystems& eqSystems,
 const std::string dofName, 
 const std::string deltaName, 
 const std::string independentDofName,
 const std::string eqSysName,
 const bool managesSolve)
  : EquationSystem(eqSystems, eqSysName),
    dofName_(dofName),
    deltaName_(deltaName),
    independentDofName_(independentDofName),
    eqSysName_(eqSysName),
    managesSolve_(managesSolve),
    dqdx_(NULL),
    qTmp_(NULL)
{
  // extract solver name and solver object
  std::string solverName = realm_.equationSystems_.get_solver_block_name(dofName);
  LinearSolver *solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_PNG);
  linsys_ = LinearSystem::create(realm_, realm_.spatialDimension_, eqSysName_, solver);

  // push back EQ to manager
  realm_.push_equation_to_systems(this);
}

//--------------------------------------------------------------------------
//-------- destructor ------------------------------------------------------
//--------------------------------------------------------------------------
ProjectedNodalGradientEquationSystem::~ProjectedNodalGradientEquationSystem()
{
  // nothing to do
}

//--------------------------------------------------------------------------
//-------- set_data_map ----------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::set_data_map( 
  BoundaryConditionType BC, std::string name)
{
  dataMap_[BC] = name;
}

//--------------------------------------------------------------------------
//-------- get_name_given_bc -----------------------------------------------
//--------------------------------------------------------------------------
std::string
ProjectedNodalGradientEquationSystem::get_name_given_bc( 
  BoundaryConditionType BC)
{
  std::map<BoundaryConditionType, std::string>::iterator it;
  it=dataMap_.find(BC);
  if ( it == dataMap_.end() )
    throw std::runtime_error("PNGEqSys::missing BC type specification (developer error)!");
  else
    return it->second;
}

//--------------------------------------------------------------------------
//-------- register_nodal_fields -------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_nodal_fields(
  stk::mesh::Part *part)
{
  stk::mesh::MetaData &meta_data = realm_.meta_data();

  const int nDim = meta_data.spatial_dimension();

  dqdx_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, dofName_));
  stk::mesh::put_field(*dqdx_, *part, nDim);

  // delta solution for linear solver
  qTmp_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, deltaName_));
  stk::mesh::put_field(*qTmp_, *part, nDim);
}

//--------------------------------------------------------------------------
//-------- register_interior_algorithm -------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_interior_algorithm(
  stk::mesh::Part *part)
{
  // types of algorithms
  const AlgorithmType algType = INTERIOR;

  // solver
  std::map<AlgorithmType, SolverAlgorithm *>::iterator its
    = solverAlgDriver_->solverAlgMap_.find(algType);
  if ( its == solverAlgDriver_->solverAlgMap_.end() ) {
    AssemblePNGElemSolverAlgorithm *theAlg
      = new AssemblePNGElemSolverAlgorithm(realm_, part, this, independentDofName_, dofName_);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    its->second->partVec_.push_back(part);
  }
}

//--------------------------------------------------------------------------
//-------- register_wall_bc ------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_wall_bc(
  stk::mesh::Part *part,
  const stk::topology &/*theTopo*/,
  const WallBoundaryConditionData &/*wallBCData*/)
{

  const AlgorithmType algType = WALL;

  // extract the field name for this bc type
  std::string fieldName = get_name_given_bc(WALL_BC);
  // create lhs/rhs algorithm;
  std::map<AlgorithmType, SolverAlgorithm *>::iterator its =
    solverAlgDriver_->solverAlgMap_.find(algType);
  if ( its == solverAlgDriver_->solverAlgMap_.end() ) {
    AssemblePNGBoundarySolverAlgorithm *theAlg
      = new AssemblePNGBoundarySolverAlgorithm(realm_, part, this, fieldName);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    its->second->partVec_.push_back(part);
  }
}

//--------------------------------------------------------------------------
//-------- register_inflow_bc ----------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_inflow_bc(
  stk::mesh::Part *part,
  const stk::topology &/*theTopo*/,
  const InflowBoundaryConditionData &/*inflowBCData*/)
{

  const AlgorithmType algType = INFLOW;

  // extract the field name for this bc type
  std::string fieldName = get_name_given_bc(INFLOW_BC);
  // create lhs/rhs algorithm;
  std::map<AlgorithmType, SolverAlgorithm *>::iterator its =
    solverAlgDriver_->solverAlgMap_.find(algType);
  if ( its == solverAlgDriver_->solverAlgMap_.end() ) {
    AssemblePNGBoundarySolverAlgorithm *theAlg
      = new AssemblePNGBoundarySolverAlgorithm(realm_, part, this, fieldName);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    its->second->partVec_.push_back(part);
  }
}

//--------------------------------------------------------------------------
//-------- register_open_bc ------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_open_bc(
  stk::mesh::Part *part,
  const stk::topology &/*theTopo*/,
  const OpenBoundaryConditionData &/*openBCData*/)
{

  const AlgorithmType algType = OPEN;

  // extract the field name for this bc type
  std::string fieldName = get_name_given_bc(OPEN_BC);
  // create lhs/rhs algorithm;
  std::map<AlgorithmType, SolverAlgorithm *>::iterator its =
    solverAlgDriver_->solverAlgMap_.find(algType);
  if ( its == solverAlgDriver_->solverAlgMap_.end() ) {
    AssemblePNGBoundarySolverAlgorithm *theAlg
      = new AssemblePNGBoundarySolverAlgorithm(realm_, part, this, fieldName);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    its->second->partVec_.push_back(part);
  }
}

//--------------------------------------------------------------------------
//-------- register_symmetry_bc --------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::register_symmetry_bc(
  stk::mesh::Part *part,
  const stk::topology &/*theTopo*/,
  const SymmetryBoundaryConditionData &/*symmetryBCData*/)
{

  const AlgorithmType algType = SYMMETRY;

  // extract the field name for this bc type
  std::string fieldName = get_name_given_bc(SYMMETRY_BC);
  // create lhs/rhs algorithm;
  std::map<AlgorithmType, SolverAlgorithm *>::iterator its =
    solverAlgDriver_->solverAlgMap_.find(algType);
  if ( its == solverAlgDriver_->solverAlgMap_.end() ) {
    AssemblePNGBoundarySolverAlgorithm *theAlg
      = new AssemblePNGBoundarySolverAlgorithm(realm_, part, this, fieldName);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    its->second->partVec_.push_back(part);
  }
}

//--------------------------------------------------------------------------
//-------- initialize ------------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::initialize()
{
  solverAlgDriver_->initialize_connectivity();
  linsys_->finalizeLinearSystem();
}

//--------------------------------------------------------------------------
//-------- reinitialize_linear_system --------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::reinitialize_linear_system()
{

  // delete linsys
  delete linsys_;

  // delete old solver
  const EquationType theEqID = EQ_PNG;
  LinearSolver *theSolver = NULL;
  std::map<EquationType, LinearSolver *>::const_iterator iter
    = realm_.root()->linearSolvers_->solvers_.find(theEqID);
  if (iter != realm_.root()->linearSolvers_->solvers_.end()) {
    theSolver = (*iter).second;
    delete theSolver;
  }

  // create new solver
  std::string solverName = realm_.equationSystems_.get_solver_block_name(dofName_);
  LinearSolver *solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_PNG);
  linsys_ = LinearSystem::create(realm_, 1, eqSysName_, solver);

  // initialize
  solverAlgDriver_->initialize_connectivity();
  linsys_->finalizeLinearSystem();
}

//--------------------------------------------------------------------------
//-------- solve_and_update ------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::solve_and_update()
{
  if ( managesSolve_ )
    solve_and_update_external();
}

//--------------------------------------------------------------------------
//-------- solve_and_update_external ------------------------------------------------
//--------------------------------------------------------------------------
void
ProjectedNodalGradientEquationSystem::solve_and_update_external()
{
  for ( int k = 0; k < maxIterations_; ++k ) {

    NaluEnv::self().naluOutputP0() << " " << k+1 << "/" << maxIterations_
                    << std::setw(15) << std::right << name_ << std::endl;
    
    // projected nodal gradient, load_complete and solve
    assemble_and_solve(qTmp_);
    
    // update
    double timeA = stk::cpu_time();
    field_axpby(
      realm_.meta_data(),
      realm_.bulk_data(),
      1.0, *qTmp_,
      1.0, *dqdx_, 
      realm_.get_activate_aura());
    double timeB = stk::cpu_time();
    timerAssemble_ += (timeB-timeA);   
  }
}

} // namespace nalu
} // namespace Sierra