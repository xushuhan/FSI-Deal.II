#include "FSI_Project.h"
#include "small_classes.h"

template <int dim>
void FSIProblem<dim>::assemble_ale_matrix_on_one_cell (const typename DoFHandler<dim>::active_cell_iterator& cell,
						       BaseScratchData<dim>& scratch,
						       PerTaskData<dim>& data )
{
  const FEValuesExtractors::Vector displacements (0);
  std::vector<Tensor<2,dim,double> > 	grad_phi_n (ale_fe.dofs_per_cell, Tensor<2,dim,double>());
  scratch.fe_values.reinit(cell);

  data.cell_matrix*=0;
  data.cell_rhs*=0;

  for (unsigned int q_point=0; q_point<scratch.n_q_points;
       ++q_point)
    {
      for (unsigned int k=0; k<ale_fe.dofs_per_cell; ++k)
  	{
  	  grad_phi_n[k] = scratch.fe_values[displacements].gradient(k, q_point);
  	}
      for (unsigned int i=0; i<ale_fe.dofs_per_cell; ++i)
  	{
  	  for (unsigned int j=0; j<ale_fe.dofs_per_cell; ++j)
  	    {
  	      data.cell_matrix(i,j)+=scalar_product(grad_phi_n[i],grad_phi_n[j])*scratch.fe_values.JxW(q_point);
  	    }
  	}
    }
  cell->get_dof_indices (data.dof_indices);
}

template <int dim>
void FSIProblem<dim>::copy_local_ale_to_global (const PerTaskData<dim>& data )
{
  if (data.assemble_matrix)
    {
      ale_constraints.distribute_local_to_global (data.cell_matrix, data.cell_rhs,
  							data.dof_indices,
  							*data.global_matrix, *data.global_rhs);
    }
}

template <int dim>
void FSIProblem<dim>::assemble_ale (Mode enum_, bool assemble_matrix)
{
  SparseMatrix<double> *ale_matrix;
  Vector<double> *ale_rhs;
  if (enum_==state)
    {
      ale_matrix = &system_matrix.block(2,2);
      ale_rhs = &system_rhs.block(2);
    }
  else if (enum_==adjoint)
    {
      ale_matrix = &adjoint_matrix.block(2,2);
      ale_rhs = &adjoint_rhs.block(2);
    }
  else
    {
      ale_matrix = &linear_matrix.block(2,2);
      ale_rhs = &linear_rhs.block(2);
    }

  if (assemble_matrix)
    {
      (*ale_matrix) *= 0;
    }
  (*ale_rhs) *= 0;

  QGauss<dim>   quadrature_formula(fem_properties.fluid_degree+2);
  FEValues<dim> fe_values (ale_fe, quadrature_formula,
			   update_values   | update_gradients |
			   update_quadrature_points | update_JxW_values);

  PerTaskData<dim> per_task_data(ale_fe, ale_matrix, ale_rhs, assemble_matrix);
  BaseScratchData<dim> scratch_data(ale_fe, quadrature_formula, update_values | update_gradients | update_quadrature_points | update_JxW_values,
				(unsigned int)enum_);

  WorkStream::run (ale_dof_handler.begin_active(),
  		   ale_dof_handler.end(),
  		   *this,
  		   &FSIProblem<dim>::assemble_ale_matrix_on_one_cell,
  		   &FSIProblem<dim>::copy_local_ale_to_global,
		   scratch_data,
  		   per_task_data);
}



template void FSIProblem<2>::assemble_ale_matrix_on_one_cell (const DoFHandler<2>::active_cell_iterator &cell,
							      BaseScratchData<2> &scratch,
							      PerTaskData<2> &data );

template void FSIProblem<2>::copy_local_ale_to_global (const PerTaskData<2> &data);

template void FSIProblem<2>::assemble_ale (Mode enum_, bool assemble_matrix);
