
#ifndef ICEPACK_ICE_STREAM_HPP
#define ICEPACK_ICE_STREAM_HPP

#include <icepack/glacier_models/depth_averaged_model.hpp>
#include <icepack/physics/basal_shear.hpp>

namespace icepack {

  /**
   * This class solves the shallow stream model of glacier flow, appropriate
   * for ice streams and ice shelves which flow with little to no vertical
   * shear.
   */
  class IceStream : public DepthAveragedModel
  {
  public:

    /**
     * Construct a model object for a given geometry and finite element basis.
     */
    IceStream(
      const Triangulation<2>& triangulation,
      const unsigned int polynomial_order
    );


    /**
     * Compute the driving stress
     \f[
     \tau = -\rho gh\nabla s
     \f]
     * from the ice thickness \f$h\f$ and surface elevation \f$s\f$.
     */
    DualVectorField<2> driving_stress(
      const Field<2>& surface,
      const Field<2>& thickness
    ) const;

    /**
     * Compute the residual of a candidate solution to the diagnostic equation.
     * This vector is used to solve the system by Newton's method.
     */
    DualVectorField<2> residual(
      const Field<2>& surface,
      const Field<2>& thickness,
      const Field<2>& temperature,
      const Field<2>& beta,
      const VectorField<2>& u,
      const DualVectorField<2>& tau_d
    ) const;

    /**
     * Compute the ice velocity from the thickness and friction coefficient.
     */
    VectorField<2> diagnostic_solve(
      const Field<2>& surface,
      const Field<2>& thickness,
      const Field<2>& temperature,
      const Field<2>& beta,
      const VectorField<2>& u0
    ) const;

    /**
     * Propagate the ice thickness forward in time using the current
     * accumulation rate, depth-averaged velocity and bed elevation.
     * For ice streams, which may encompass both floating and grounded ice,
     * we need to return both the updated thickness and surface elevation;
     * to do so, we also require the bed elevation field.
     */
    std::pair<Field<2>, Field<2> > prognostic_solve(
      const double dt,
      const Field<2>& bed,
      const Field<2>& thickness,
      const Field<2>& accumulation,
      const VectorField<2>& u
    ) const;

    /**
     * Solve the linearization of the diagnostic equation around a velocity u0
     * with a given VectorField as right-hand side
     */
    VectorField<2> adjoint_solve(
      const Field<2>& surface,
      const Field<2>& thickness,
      const Field<2>& temperature,
      const Field<2>& beta,
      const VectorField<2>& u0,
      const DualVectorField<2>& f
    ) const;

    /**
     * Function object for computing the basal friction coefficient
     */
    const BasalShear basal_shear;
  };

}


#endif
