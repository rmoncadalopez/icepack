
#ifndef ICEPACK_INVERSE_REGULARIZATION_HPP
#define ICEPACK_INVERSE_REGULARIZATION_HPP

#include <deal.II/lac/linear_operator.h>

#include <icepack/field.hpp>
#include <icepack/numerics/linear_solve.hpp>

namespace icepack {
  namespace inverse {

    using dealii::linear_operator;

    /**
     * This class contains procedures for regularizing the solution of an
     * inverse problem by penalizing the square gradient:
     \f[
     R[u; \alpha] = \frac{\alpha^2}{2}\int_\Omega|\nabla u|^2 dx.
     \f]
     * Penalizing the square gradient is equivalent to applying a low-pass
     * filter to the solution with smoothing length \f$\alpha\f$.
     */
    template <int rank, int dim>
    class SquareGradient
    {
    public:
      SquareGradient(const Discretization<dim>& dsc, const double alpha)
        :
        L(get<rank>(dsc).get_sparsity()),
        M(&get<rank>(dsc).get_mass_matrix())
      {
        const auto& field_dsc = get<rank>(dsc);

        dealii::ConstantFunction<dim> Alpha2(alpha * alpha);

        dealii::MatrixCreator::create_laplace_matrix(
          field_dsc.get_dof_handler(),
          dsc.quad(),
          L,
          &Alpha2,
          field_dsc.get_constraints()
        );
      }

      /**
       * Evaluate the integrated square gradient of a field.
       */
      double operator()(const FieldType<rank, dim>& u) const
      {
        return 0.5 * L.matrix_norm_square(u.get_coefficients());
      }

      /**
       * Compute the derivative of the square gradient of a field, i.e. the
       * Laplace operator applied to the field
       */
      FieldType<rank, dim, dual>
      derivative(const FieldType<rank, dim>& u) const
      {
        FieldType<rank, dim, dual> laplacian_u(u.get_discretization());
        L.vmult(laplacian_u.get_coefficients(), u.get_coefficients());
        return laplacian_u;
      }

      /**
       * Compute the field \f$u\f$ such that \f$u^*\f$ is closest to \f$f\f$,
       * subject to a penalty on the square gradient
       */
      FieldType<rank, dim>
      filter(
        const FieldType<rank, dim, primal>&,
        const FieldType<rank, dim, dual>& f
      ) const
      {
        FieldType<rank, dim> u(f.get_discretization());

        const auto A = linear_operator(*M) + linear_operator(L);

        SolverControl solver_control(1000, 1.0e-10);
        SolverCG<> solver(solver_control);

        // TODO: use an actual preconditioner
        dealii::PreconditionIdentity P;
        solver.solve(A, u.get_coefficients(), f.get_coefficients(), P);

        return u;
      }

    protected:
      SparseMatrix<double> L;
      SmartPointer<const SparseMatrix<double> > M;
    };


    /**
     * This class contains procedures for regularizing the solution of an
     * inverse problem by penalizing the total variation:
     \f[
     R[u; \alpha] =
     \int_\Omega\left(\sqrt{\alpha^2|\nabla u|^2 + 1} - 1\right)dx
     \f]
     * Strictly speaking, this is the pseudo-Heuber total variation, which is
     * rounded off in order to make the functional differentiable.
     *
     * The total variation of a function can be visualized as the lateral
     * surface area of its graph. Like the square gradient functional,
     * penalizing the total variation is an effective way to eliminated
     * spurious oscillations in the solution of an inverse problem constrained
     * by noisy data. Unlike low-pass filtering, however, total variation
     * filtering does not remove all steep gradients or jump discontinuities.
     * Instead, it tends to confine these interfaces to as small a perimeter as
     * possible where they do exist.
     */
    template <int rank, int dim>
    class TotalVariation
    {
    public:
      TotalVariation(const Discretization<dim>&, const double alpha)
        :
        alpha(alpha)
      {}

      /**
       * Compute the total variation of a field.
       */
      double operator()(const FieldType<rank, dim>& u) const
      {
        using gradient_type = typename FieldType<rank, dim>::gradient_type;

        const QGauss<dim> quad = u.get_discretization().quad();

        FEValues<dim> fe_values(u.get_fe(), quad, DefaultUpdateFlags::flags);
        const typename FieldType<rank, dim>::extractor_type ex(0);

        const unsigned int n_q_points = quad.size();
        std::vector<gradient_type> du_values(n_q_points);

        double total_variation = 0.0;

        for (auto cell: u.get_dof_handler().active_cell_iterators()) {
          fe_values.reinit(cell);

          fe_values[ex].get_function_gradients(
            u.get_coefficients(), du_values
          );

          for (unsigned int q = 0; q < n_q_points; ++q) {
            const double dx = fe_values.JxW(q);
            const gradient_type du = alpha * du_values[q];
            const double cell_graph_area = std::sqrt(du*du + 1) - 1;
            total_variation += cell_graph_area * dx;
          }
        }

        return total_variation;
      }


      /**
       * Compute the derivative of the total variation of a field \f$u\f$; the
       * derivative of the total variation is a nonlinear elliptic operator,
       * which is related to the minimal surface equation, applied to \f$u\f$.
       */
      FieldType<rank, dim, dual>
      derivative(const FieldType<rank, dim>& u) const
      {
        using gradient_type = typename FieldType<rank, dim>::gradient_type;

        const auto& discretization = u.get_discretization();
        FieldType<rank, dim, dual> div_graph_normal(discretization);

        const auto& fe = u.get_fe();
        const auto& dof_handler = u.get_dof_handler();

        const QGauss<dim> quad = discretization.quad();

        FEValues<dim> fe_values(fe, quad, DefaultUpdateFlags::flags);
        const typename FieldType<rank, dim>::extractor_type ex(0);

        const unsigned int n_q_points = quad.size();
        const unsigned int dofs_per_cell = fe.dofs_per_cell;

        std::vector<gradient_type> du_values(n_q_points);

        Vector<double> cell_div_graph_normal(dofs_per_cell);
        std::vector<dealii::types::global_dof_index> local_dof_indices(dofs_per_cell);

        for (auto cell: dof_handler.active_cell_iterators()) {
          cell_div_graph_normal = 0;
          fe_values.reinit(cell);

          fe_values[ex].get_function_gradients(
            u.get_coefficients(), du_values
          );

          for (unsigned int q = 0; q < n_q_points; ++q) {
            const double dx = fe_values.JxW(q);
            const gradient_type du = alpha * du_values[q];
            const double dA = std::sqrt(du*du + 1);

            for (unsigned int i = 0; i < dofs_per_cell; ++i) {
              const gradient_type dphi = fe_values[ex].gradient(i, q);
              cell_div_graph_normal(i) += alpha * du * dphi / dA * dx;
            }
          }

          cell->get_dof_indices(local_dof_indices);
          u.get_constraints().distribute_local_to_global(
            cell_div_graph_normal,
            local_dof_indices,
            div_graph_normal.get_coefficients()
          );
        }

        return div_graph_normal;
      }


      /**
       * Apply a filter to the dual field \f$f\f$ which matches it as best as
       * possible subject to a constraint on the total variation of the output,
       * which is linearized around an input field \f$u\f$.
       *
       * The Hessian of the total variation is an anisotropic elliptic operator
       * where the anisotropy is aligned with the gradient of the input field
       * \f$u\f$.
       */
      FieldType<rank, dim>
      filter(
        const FieldType<rank, dim, primal>& u,
        const FieldType<rank, dim, dual>& f
      ) const
      {
        // TODO: use matrix-free method w. multigrid + Chebyshev preconditioner

        using value_type = typename FieldType<rank, dim>::value_type;
        using gradient_type = typename FieldType<rank, dim>::gradient_type;

        const auto& discretization = u.get_discretization();
        FieldType<rank, dim> v(discretization);

        SparseMatrix<double> A(get<rank>(discretization).get_sparsity());
        A = 0;

        const auto& fe = u.get_fe();
        const auto& dof_handler = u.get_dof_handler();

        const QGauss<dim> quad = discretization.quad();

        FEValues<dim> fe_values(fe, quad, DefaultUpdateFlags::flags);
        const typename FieldType<rank, dim>::extractor_type ex(0);

        const unsigned int n_q_points = quad.size();
        const unsigned int dofs_per_cell = fe.dofs_per_cell;

        std::vector<gradient_type> du_values(n_q_points);

        dealii::FullMatrix<double> cell_matrix(dofs_per_cell, dofs_per_cell);
        std::vector<dealii::types::global_dof_index> local_dof_indices(dofs_per_cell);

        for (auto cell: dof_handler.active_cell_iterators()) {
          cell_matrix = 0;
          fe_values.reinit(cell);

          fe_values.get_function_gradients(u.get_coefficients(), du_values);
          for (unsigned int q = 0; q < n_q_points; ++q) {
            const double dx = fe_values.JxW(q);
            const gradient_type du = alpha * du_values[q];
            const double dA = std::sqrt(du*du + 1);

            for (unsigned int i = 0; i < dofs_per_cell; ++i) {
              const auto phi_i = fe_values[ex].value(i, q);
              const auto d_phi_i = fe_values[ex].gradient(i, q);
              for (unsigned int j = 0; j < dofs_per_cell; ++j) {
                const auto phi_j = fe_values[ex].value(j, q);
                const auto d_phi_j = fe_values[ex].gradient(j, q);

                const double cell_mass = phi_i * phi_j;
                const auto tau = du / dA;
                const double cell_div_graph_normal =
                  (d_phi_i * d_phi_j - (d_phi_i * tau) * (tau * d_phi_j)) / dA;
                cell_matrix(i, j) +=
                  (cell_mass + alpha*alpha * cell_div_graph_normal) * dx;
              }
            }
          }

          cell->get_dof_indices(local_dof_indices);
          u.get_constraints().distribute_local_to_global(
            cell_matrix, local_dof_indices, A
          );
        }

        A.compress(dealii::VectorOperation::add);

        linear_solve(
          A, v.get_coefficients(), f.get_coefficients(), u.get_constraints()
        );

        return v;
      }

    protected:
      const double alpha;
    };


  } // End of namespace inverse
} // End of namespace icepack

#endif
