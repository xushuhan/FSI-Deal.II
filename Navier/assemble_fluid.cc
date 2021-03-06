#include "FSI_Project.h"
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/std_cxx1x/condition_variable.h>
#include <deal.II/base/std_cxx1x/condition_variable.h>
#include <deal.II/base/std_cxx1x/type_traits.h>

// #include <deal.II/grid/grid_out.h> /* remove this when not adding temporary code in assembly */

// regexp replacement for grad_* terms:
// regexp-replace: \(grad[^
//                     \*\(]+\)\*
// with:
//       transpose(\1)*
// then fix the assembly of the Stokes operator and rhs (lines ~420 and ~540)

template <int dim>
void FSIProblem<dim>::fluid_state_solve(unsigned int initialized_timestep_number) {
  // solution_star.block(0)=1;
  bool newton = fem_properties.fluid_newton;
  unsigned int picard_iterations = 1;
  unsigned int loop_count = 0;
  do  {
    solution_star.block(0)=solution.block(0);
    //timer.enter_subsection ("Assemble");
    if (loop_count < picard_iterations) fem_properties.fluid_newton = false; 
    // Turn off Newton's method for a few picard iterations
    assemble_fluid(state, true);
    if (loop_count < picard_iterations) fem_properties.fluid_newton = newton;
    //timer.leave_subsection();

    dirichlet_boundaries((System)0,state);
    //timer.enter_subsection ("State Solve"); 
    if (timestep_number==initialized_timestep_number) {
      state_solver[0].initialize(system_matrix.block(0,0));
    } else {
      state_solver[0].factorize(system_matrix.block(0,0));
    }
    solve(state_solver[0],0,state);
	      
    // Pressure needs rescaled, since it was scaled/balanced against rho_f  in the operator
    // tmp = 0; tmp2 = 0;
    // transfer_all_dofs(solution, tmp, 0, 2);
    // transfer_all_dofs(tmp2, solution, 2, 0);
    // solution.block(0) *= physical_properties.rho_f;
    // transfer_all_dofs(tmp, solution, 2, 0);


    // This is done by:
    // copying out all except pressure
    // copying in zeros over all but pressure
    // scaling the pressure
    // copying the other values back in

    //timer.leave_subsection ();
    solution_star.block(0)-=solution.block(0);
    //++total_solves;
    if ((fem_properties.richardson && !fem_properties.fluid_newton) || !physical_properties.navier_stokes) {
      break;
    } else {
      std::cout << "F: " << solution_star.block(0).l2_norm() << std::endl;
    }
	      
    loop_count++;
  } while (solution_star.block(0).l2_norm()>1e-8);
  solution_star.block(0) = solution.block(0); 
}

template <int dim>
void FSIProblem<dim>::assemble_fluid_matrix_on_one_cell (const typename DoFHandler<dim>::active_cell_iterator& cell,
						       FullScratchData<dim>& scratch,
						       PerTaskData<dim>& data )
{
  unsigned int state=0, adjoint=1;//, linear=2;

  ConditionalOStream pcout(std::cout,Threads::this_thread_id()==master_thread); 
  //TimerOutput timer (pcout, TimerOutput::summary,
  //		     TimerOutput::wall_times); 
  //timer.enter_subsection ("Beginning");

  FluidStressValues<dim> fluid_stress_values(physical_properties);
  FluidBoundaryValues<dim> fluid_boundary_values_function(physical_properties, fem_properties);
  fluid_boundary_values_function.set_time (time);

  const FEValuesExtractors::Vector velocities (0);
  const FEValuesExtractors::Scalar pressure (dim);

  std::vector<Vector<double> > old_solution_values(scratch.n_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > old_old_solution_values(scratch.n_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > adjoint_rhs_values(scratch.n_face_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > linear_rhs_values(scratch.n_face_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > u_star_values(scratch.n_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > z(scratch.n_q_points, Vector<double>(dim+1));

  std::vector<Tensor<2,dim,double> > grad_u_old (scratch.n_q_points, Tensor<2,dim,double>());
  std::vector<Tensor<2,dim,double> > grad_u_old_old (scratch.n_q_points, Tensor<2,dim,double>());
  std::vector<Tensor<2,dim,double> > grad_u_star (scratch.n_q_points, Tensor<2,dim,double>());
  std::vector<Tensor<2,dim,double> > F (scratch.n_q_points, Tensor<2,dim,double>());
  std::vector<Tensor<2,dim,double> > grad_z (scratch.n_q_points, Tensor<2,dim,double>());

  std::vector<Tensor<1,dim,double> > stress_values (dim+1, Tensor<1,dim,double>());
  Vector<double> u_true_side_values (dim+1);
  std::vector<Vector<double> > g_stress_values(scratch.n_face_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > old_solution_side_values(scratch.n_face_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > old_old_solution_side_values(scratch.n_face_q_points, Vector<double>(dim+1));
  std::vector<Vector<double> > u_star_side_values(scratch.n_face_q_points, Vector<double>(dim+1));

  std::vector<Tensor<1,dim,double> > 		  phi_u (fluid_fe.dofs_per_cell, Tensor<1,dim,double>());
 
  std::vector<Tensor<2,dim,double> > 		  grad_phi_u (fluid_fe.dofs_per_cell, Tensor<2,dim,double>());
  std::vector<double>                     div_phi_u   (fluid_fe.dofs_per_cell);
  std::vector<double>                     phi_p       (fluid_fe.dofs_per_cell);

  /*
    This is a quick test to give a sanity check for how tensor arithmetic is completed.
    Assuming u and v are both column vectors,

    u \cdot \grad u, v
    should be the same as ((\transpose(grad u)' )* u)' * v in normal matrix vector multiplies,
    and using deal.II tensor operations, it would be written ((\transpose(grad u)' )* u) * v
    since deal.II assumes dot product of two dim 1 vectors.

    However, since u and v are row vectors in deal.II,
    u \cdot \grad u, v
    should be the same as ((\transpose(grad u)' )* u')' * v in normal matrix vector mutliplies,
    and using deal.II tensor operations, it would be written ((\transpose(grad u)' )* u) * v
    since deal.II assumes dot product of two dim 1 vectors.

    Because a Tensor<1,dim> * Tensor<2,dim> gives you rows of the first Tensor<1,dim> 
    time columns of the Tensor<2,dim> it is equivalent to u*\grad u in deal.II is 
    equivalent to (\transpose(grad u)' )* u, as it should be.

    At the end of the day, u * \transpose(grad u )* v does exactly what (u \cdot \grad u, v)
    should. This means that you must use the correct \grad u for Navier-Stokes!!!
    NOT the deal.ii gradient, which is the Jacobian.

  Tensor<2,dim> test_grad;
  test_grad[0][0]=2;
  test_grad[0][1]=-10;
  test_grad[1][0]=5;
  test_grad[1][1]=17;

  Tensor<1,dim> left_vect;
  left_vect[0]=40;
  left_vect[1]=-30;

  Tensor<1,dim> right_vect;
  right_vect[0]=75;
  right_vect[1]=15;

  std::cout << left_vect * test_transpose(grad )* right_vect << std::endl; */

  data.cell_matrix*=0;
  data.cell_rhs*=0;

  scratch.fe_values.reinit(cell);

  if (data.assemble_matrix)
    {
      AssertThrow(!(fem_properties.richardson && !fem_properties.fluid_newton && physical_properties.navier_stokes && physical_properties.stability_terms),ExcNotImplemented());

      scratch.fe_values.get_function_values (old_solution.block(0), old_solution_values);
      scratch.fe_values.get_function_values (solution_star.block(0),u_star_values);
      scratch.fe_values[velocities].get_function_gradients(old_solution.block(0),grad_u_old); // THESE NEED TO BE TRANSPOSED IN THE FUTURE
      scratch.fe_values[velocities].get_function_gradients(solution_star.block(0),grad_u_star); // THESE ALSO NEED TO BE TRANSPOSED, since Deal.II transposes gradients

      scratch.fe_values.get_function_values (old_old_solution.block(0), old_old_solution_values);
      if (fem_properties.richardson && !fem_properties.fluid_newton)
	{
	  scratch.fe_values.get_function_values (old_old_solution.block(0), old_old_solution_values);
	  scratch.fe_values[velocities].get_function_gradients(old_old_solution.block(0),grad_u_old_old);
	}
      if (physical_properties.moving_domain) 
	{
	  scratch.fe_values.get_function_values (mesh_velocity.block(0), z);
	  scratch.fe_values[velocities].get_function_gradients(mesh_velocity.block(0),grad_z);
	}

      for (unsigned int q=0; q<scratch.n_q_points; ++q)
	{
	  Tensor<1,dim,double> u_star, u_old, u_old_old, meshvelocity;
	  for (unsigned int d=0; d<dim; ++d)
	    {
	      u_star[d] = u_star_values[q](d);
	      u_old[d] = old_solution_values[q](d);
	      u_old_old[d] = old_old_solution_values[q](d);
	      if (fem_properties.richardson && !fem_properties.fluid_newton)
		{
		  u_old_old[d] = old_old_solution_values[q](d);
		}
	      if (physical_properties.moving_domain) 
		{
		  meshvelocity[d] = z[q](d);
		}
	    }

	  for (unsigned int k=0; k<fluid_fe.dofs_per_cell; ++k)
	    {
	      phi_u[k]	   = scratch.fe_values[velocities].value (k, q);
	      grad_phi_u[k]    = scratch.fe_values[velocities].gradient (k, q);
	      phi_p[k]         = scratch.fe_values[pressure].value (k, q);
	    }
	  for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
	    {
	      //        
	      // LOOP TO BUILD LHS OVER DOMAIN BEGINS HERE       
	      // 
	      for (unsigned int j=0; j<fluid_fe.dofs_per_cell; ++j)
		{
		  double epsilon = 0;
		  if (physical_properties.simulation_type==2)
		    epsilon = 0;//1e-11; // only when all Dirichlet b.c.s

		  if (physical_properties.stability_terms)
		    {
		      if (scratch.mode_type==state)
			{
			  if (physical_properties.navier_stokes)
			    {
			      // assumes not (fem_properties.richardson && !fem_properties.fluid_newton && physical_properties.stability_terms)
			      if (fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    ( 
				     phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
				     - phi_u[j]*transpose(grad_phi_u[i])*u_star
				     + u_star*transpose(grad_phi_u[j])*phi_u[i]
				     - u_star*transpose(grad_phi_u[i])*phi_u[j]
				      ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    (
				     // phi_u[j]*transpose(grad_u_star[q])*phi_u[i] <-- Ineffective way
				     // - phi_u[j]*transpose(grad_phi_u[i])*u_star
				     u_star*transpose(grad_phi_u[j])*phi_u[i]
				     - u_star*transpose(grad_phi_u[i])*phi_u[j]
				     ) * scratch.fe_values.JxW(q);
				}
			      data.cell_matrix(i,j) += 0.5 * (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				(
				 phi_u[j]*transpose(grad_u_old[q])*phi_u[i]
				 - phi_u[j]*transpose(grad_phi_u[i])*u_old
				 + u_old*transpose(grad_phi_u[j])*phi_u[i]
				 - u_old*transpose(grad_phi_u[i])*phi_u[j]
				 ) * scratch.fe_values.JxW(q);
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += 0.5 * fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- (meshvelocity*transpose(grad_phi_u[j]))*phi_u[i]
				 + (meshvelocity*transpose(grad_phi_u[i]))*phi_u[j]
				 - trace(grad_z[q]) * phi_u[j] * phi_u[i]
			  	 )* scratch.fe_values.JxW(q);
			    }
			}
		      else if (scratch.mode_type==adjoint) 
			{
			  if (physical_properties.navier_stokes)
			    {
			      if (fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    ( 
				     phi_u[i]*transpose(grad_u_star[q])*phi_u[j]
				     - phi_u[i]*transpose(grad_phi_u[j])*u_star
				     + u_star*transpose(grad_phi_u[i])*phi_u[j]
				     - u_star*transpose(grad_phi_u[j])*phi_u[i]
				      ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    (
				     phi_u[i]*transpose(grad_u_star[q])*phi_u[j]
				     - phi_u[i]*transpose(grad_phi_u[j])*u_star
				     + u_star*transpose(grad_phi_u[i])*phi_u[j]
				     - u_star*transpose(grad_phi_u[j])*phi_u[i]
				     ) * scratch.fe_values.JxW(q);
				}
			      data.cell_matrix(i,j) += 0.5 * (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				(
				 phi_u[i]*transpose(grad_u_old[q])*phi_u[j]
				 - phi_u[i]*transpose(grad_phi_u[j])*u_old
				 + u_old*transpose(grad_phi_u[i])*phi_u[j]
				 - u_old*transpose(grad_phi_u[j])*phi_u[i]
				 ) * scratch.fe_values.JxW(q);
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += 0.5 * fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- (meshvelocity*transpose(grad_phi_u[i]))*phi_u[j]
				 + (meshvelocity*transpose(grad_phi_u[j]))*phi_u[i]
				 - trace(grad_z[q]) * phi_u[i] * phi_u[j]
			  	 )* scratch.fe_values.JxW(q);
			    }
			}
		      else // scratch.mode_type==linear
			{
			  if (physical_properties.navier_stokes)
			    {
			      if (fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    ( 
				     phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
				     - phi_u[j]*transpose(grad_phi_u[i])*u_star
				     + u_star*transpose(grad_phi_u[j])*phi_u[i]
				     - u_star*transpose(grad_phi_u[i])*phi_u[j]
				      ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
				    (
				     phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
				     - phi_u[j]*transpose(grad_phi_u[i])*u_star
				     + u_star*transpose(grad_phi_u[j])*phi_u[i]
				     - u_star*transpose(grad_phi_u[i])*phi_u[j]
				     ) * scratch.fe_values.JxW(q);
				}
			      data.cell_matrix(i,j) += 0.5 * (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				(
				 phi_u[j]*transpose(grad_u_old[q])*phi_u[i]
				 - phi_u[j]*transpose(grad_phi_u[i])*u_old
				 + u_old*transpose(grad_phi_u[j])*phi_u[i]
				 - u_old*transpose(grad_phi_u[i])*phi_u[j]
				 ) * scratch.fe_values.JxW(q);
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += 0.5 * fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- meshvelocity*transpose(grad_phi_u[j])*phi_u[i]
				 + meshvelocity*transpose(grad_phi_u[i])*phi_u[j]
				 - trace(grad_z[q]) * phi_u[j] * phi_u[i]
			  	 )* scratch.fe_values.JxW(q);
			    }
			}
		    }
		  else // stability_terms is false
		    {
		      if (scratch.mode_type==state)
			{
			  if (physical_properties.navier_stokes)
			    {
			      if (fem_properties.richardson && !fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     (1.5*u_old-.5*u_old_old)*transpose(grad_phi_u[j])*phi_u[i]
				     ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  if (fem_properties.fluid_newton)
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					( 
					 phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
					 + u_star*transpose(grad_phi_u[j])*phi_u[i]
					  ) * scratch.fe_values.JxW(q);
				    }
				  else
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					(
					 //phi_u[j]*transpose(grad_u_star[q])*phi_u[i] <- This way is ineffective
					 u_star*transpose(grad_phi_u[j])*phi_u[i]
					 ) * scratch.fe_values.JxW(q);
				    }
				  data.cell_matrix(i,j) += (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     phi_u[j]*transpose(grad_u_old[q])*phi_u[i]
				     +u_old*transpose(grad_phi_u[j])*phi_u[i]
				     ) * scratch.fe_values.JxW(q);
				}
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- (meshvelocity*transpose(grad_phi_u[j]))*phi_u[i]
				 //- trace(grad_z[q]) * phi_u[j] * phi_u[i]
			  	 )* scratch.fe_values.JxW(q);
			    }
			}
		      else if (scratch.mode_type==adjoint) 
			{
			  if (physical_properties.navier_stokes)
			    {
			      if (fem_properties.richardson && !fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     (1.5*u_old-.5*u_old_old)*transpose(grad_phi_u[i])*phi_u[j]
				     ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  if (fem_properties.fluid_newton)
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					(
					 phi_u[i]*transpose(grad_u_star[q])*phi_u[j]
					 + u_star*transpose(grad_phi_u[i])*phi_u[j]
					 ) * scratch.fe_values.JxW(q);
				    }
				  else 
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					(
					 phi_u[i]*transpose(grad_u_star[q])*phi_u[j]
					 + u_star*transpose(grad_phi_u[i])*phi_u[j]
					 ) * scratch.fe_values.JxW(q);
				    }
				  data.cell_matrix(i,j) += (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     phi_u[i]*transpose(grad_u_old[q])*phi_u[j]
				     +u_old*transpose(grad_phi_u[i])*phi_u[j]
				     ) * scratch.fe_values.JxW(q);
				}
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- meshvelocity*transpose(grad_phi_u[i])*phi_u[j]
				 //- trace(grad_z[q]) * phi_u[i] * phi_u[j]
			  	 )* scratch.fe_values.JxW(q);
			    }
			}
		      else // scratch.mode_type==linear
			{
			  if (physical_properties.navier_stokes)
			    {
			      if (fem_properties.richardson && !fem_properties.fluid_newton)
				{
				  data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     (1.5*u_old-.5*u_old_old)*transpose(grad_phi_u[j])*phi_u[i]
				     ) * scratch.fe_values.JxW(q);
				}
			      else
				{
				  if (fem_properties.fluid_newton)
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					(
					 phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
					 + u_star*transpose(grad_phi_u[j])*phi_u[i]
					 ) * scratch.fe_values.JxW(q);
				    }
				  else
				    {
				      data.cell_matrix(i,j) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f * 
					(
					 phi_u[j]*transpose(grad_u_star[q])*phi_u[i]
					 + u_star*transpose(grad_phi_u[j])*phi_u[i]
					 ) * scratch.fe_values.JxW(q);
				    }
				  data.cell_matrix(i,j) += (1-fem_properties.fluid_theta)*fem_properties.fluid_theta * physical_properties.rho_f * 
				    (
				     phi_u[j]*transpose(grad_u_old[q])*phi_u[i]
				     +u_old*transpose(grad_phi_u[j])*phi_u[i]
				     ) * scratch.fe_values.JxW(q);
				}
			    }
			  if (physical_properties.moving_domain)
			    {
			      data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f *
			  	(- u_old_old/time_step*transpose(grad_phi_u[j])*phi_u[i]
				 //- trace(grad_z[q]) * phi_u[j] * phi_u[i]
			  	 )* scratch.fe_values.JxW(q);
			      // data.cell_matrix(i,j) += fem_properties.fluid_theta * physical_properties.rho_f *
			      // 	(- meshvelocity*transpose(grad_phi_u[j])*phi_u[i]
			      // 	 //- trace(grad_z[q]) * phi_u[j] * phi_u[i]
			      // 	 )* scratch.fe_values.JxW(q);
			    }
			}
		    }

		  if (fem_properties.time_dependent) {
		    data.cell_matrix(i,j) +=  physical_properties.rho_f/time_step*phi_u[i]*phi_u[j] * scratch.fe_values.JxW(q);
		  }
		  data.cell_matrix(i,j) += ( fem_properties.fluid_theta * ( 2*physical_properties.viscosity
							     // In the case of the Laplacian, the gradients in the scalar_product should be transposed, but
							     // it is cheaper not to transpose both terms (and it is equivalent). 
							       
							     // For the symmetric tensor, it is okay to use the deformation tensor of test functions
							     *0.25*scalar_product(grad_phi_u[j]+transpose(grad_phi_u[j]),grad_phi_u[i]+transpose(grad_phi_u[i])))
							       
					     // Using a nonsymmetric tensor, it can be observed that the deformation tensor of the test
					     // functions drives the solution towards another solution
					     //*0.5*scalar_product(transpose(grad_phi_u[j]),.5*(grad_phi_u[i]+transpose(grad_phi_u[i]))))
					     //+ scalar_product(grad_phi_u[j],grad_phi_u[i])
					     // Only the gradient of the test functions can be used for a nonsymmetric tensor
					     //*0.5*scalar_product(grad_phi_u[j],grad_phi_u[i]))
					     // same is true here about not transposing since its effect is lost (equivalent)
					     // - physical_properties.rho_f*trace(grad_phi_u[i]) * phi_p[j] // (p,\div v)  momentum  -- Multiplier by physical_properties.rho_f is just for conditioning.
					     // - physical_properties.rho_f*phi_p[i] * trace(grad_phi_u[j]) // (\div u, q) mass         It must be scaled back after the solve 
					     - trace(grad_phi_u[i]) * phi_p[j] // (p,\div v)  momentum  -- Multiplier by physical_properties.rho_f is just for conditioning.
					     - phi_p[i] * trace(grad_phi_u[j]) // (\div u, q) mass         It must be scaled back after the solve 
					     - epsilon * phi_p[i] * phi_p[j])
		    * scratch.fe_values.JxW(q);
		}
	    }

	  //        
	  // LOOP TO BUILD RHS OVER DOMAIN BEGINS HERE       
	  // 

	  if (scratch.mode_type==state)
	    for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
	      {
		//const double old_p = old_solution_values[q](dim);
		Tensor<1,dim,double> old_u;
		for (unsigned int d=0; d<dim; ++d)
		  old_u[d] = old_solution_values[q](d);
		const Tensor<1,dim,double> phi_i_s      = scratch.fe_values[velocities].value (i, q);
		//const Tensor<2,dim> symgrad_phi_i_s = scratch.fe_values[velocities].symmetric_gradient (i, q);
		//const double div_phi_i_s =  scratch.fe_values[velocities].divergence (i, q);
		const Tensor<2,dim,double> grad_phi_i_s = scratch.fe_values[velocities].gradient (i, q);
		//const double div_phi_i_s =  scratch.fe_values[velocities].divergence (i, q);
		if (physical_properties.navier_stokes)
		  {
		    if (fem_properties.richardson && !fem_properties.fluid_newton)
		      {
		        // assumes not (fem_properties.richardson && !fem_properties.fluid_newton && physical_properties.stability_terms)
			data.cell_rhs(i) -= (1-fem_properties.fluid_theta) * physical_properties.rho_f * 
			  (
			   (1.5*u_old-.5*u_old_old)*transpose(grad_u_old[q])*phi_i_s
			   ) * scratch.fe_values.JxW(q);
		      }
		    else
		      {
			if (fem_properties.fluid_newton) 
			  {
			    if (physical_properties.stability_terms)
			      {
				data.cell_rhs(i) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
				  * (
				     u_star*transpose(grad_u_star[q])*phi_i_s
				     - u_star*transpose(grad_phi_i_s)*u_star
				     ) * scratch.fe_values.JxW(q);
			      }
			    else
			      {
				data.cell_rhs(i) += pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
				  * (
				     u_star*transpose(grad_u_star[q])*phi_i_s
				     ) * scratch.fe_values.JxW(q);
			      }
			  }
			if (physical_properties.stability_terms)
			  {
			    data.cell_rhs(i) -= 0.5 * pow(1-fem_properties.fluid_theta,2) * physical_properties.rho_f 
			      *( u_old*transpose(grad_u_old[q])*phi_i_s 
				 - u_old*transpose(grad_phi_i_s)*u_old 
				 )* scratch.fe_values.JxW(q);
			  }
			else
			  {
			    data.cell_rhs(i) -= pow(1-fem_properties.fluid_theta,2) * physical_properties.rho_f 
			      * u_old*transpose(grad_u_old[q])*phi_i_s * scratch.fe_values.JxW(q);
			  }
		      }
		  }
		if (physical_properties.moving_domain) 
		  {
		    if (physical_properties.stability_terms)
		      {
			data.cell_rhs(i) -= 0.5 * (1 - fem_properties.fluid_theta) * physical_properties.rho_f 
			  * (
			     - meshvelocity*transpose(grad_u_old[q])*phi_i_s
			     + meshvelocity*transpose(grad_phi_i_s)*u_old
			     - trace(grad_z[q]) * u_old * phi_i_s
			     ) * scratch.fe_values.JxW(q);
		      }
		    else
		      {
			data.cell_rhs(i) -= (1 - fem_properties.fluid_theta) * physical_properties.rho_f 
			  * (
			     - meshvelocity*transpose(grad_u_old[q])*phi_i_s
			     //- trace(grad_z[q]) * u_old * phi_i_s
			     ) * scratch.fe_values.JxW(q);
		      }
		  }
		if (fem_properties.time_dependent) {
		  data.cell_rhs(i) += physical_properties.rho_f/time_step *phi_i_s*old_u * scratch.fe_values.JxW(q);
		}
		data.cell_rhs(i) += ((1-fem_properties.fluid_theta) * (-2*physical_properties.viscosity
					*0.25*scalar_product(grad_u_old[q]+transpose(grad_u_old[q]),grad_phi_i_s+transpose(grad_phi_i_s))

					//+ scalar_product(grad_u_star[q],grad_phi_i_s)
					//*0.5*scalar_product(grad_u_old[q]+transpose(grad_u_old[q]),grad_phi_i_s)
					//*scalar_product(grad_u_old[q],grad_phi_i_s)
					//*(-2*physical_properties.viscosity
					//*(transpose(grad_u_old[q][0][0])*symgrad_phi_i_s[0][0]
					//+ 0.5*(transpose(grad_u_old[q][1][0]+grad_u_old[q][0][1]))*(symgrad_phi_i_s[1][0]+symgrad_phi_i_s[0][1])
					//+ transpose(grad_u_old[q][1][1])*symgrad_phi_i_s[1][1]
					)
				     ) * scratch.fe_values.JxW(q);
			  
	      }
	}
    }

  for (unsigned int face_no=0;
       face_no<GeometryInfo<dim>::faces_per_cell;
       ++face_no)
    {
      if (cell->at_boundary(face_no))
	{
	  if (fluid_boundaries[cell->face(face_no)->boundary_indicator()]==Neumann || fluid_boundaries[cell->face(face_no)->boundary_indicator()]==DoNothing)
	    {
	      scratch.fe_face_values.reinit (cell, face_no);

	      if (data.assemble_matrix)
	      if (physical_properties.navier_stokes && physical_properties.stability_terms)
	      	{
	      	  scratch.fe_face_values.get_function_values (old_solution.block(0), old_solution_side_values);
	      	  scratch.fe_face_values.get_function_values (solution_star.block(0), u_star_side_values);
	      	  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
	      	    {
	      	      Tensor<1,dim,double> u_old_old_side, u_old_side, u_star_side;
	      	      for (unsigned int d=0; d<dim; ++d)
	      		{
	      		  u_old_old_side[d] = old_old_solution_side_values[q](d);
	      		  u_old_side[d] = old_solution_side_values[q](d);
	      		  u_star_side[d] = u_star_side_values[q](d);
	      		}
	      	      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
	      		{
	      		  for (unsigned int j=0; j<fluid_fe.dofs_per_cell; ++j)
	      		    {
	      		      if (scratch.mode_type==state)
	      			{
				  if (fem_properties.fluid_newton)
				    {
				      data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
					*( 
					  (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
					  +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
					   ) * scratch.fe_face_values.JxW(q);
				    }
				  else
				    {
				      data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
					*( 
					  (u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
					   ) * scratch.fe_face_values.JxW(q);
				    }
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
				      + (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		      else if (scratch.mode_type==adjoint) 
	      			{
	      			  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
	      			    *( 
	      			      (scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (j, q))
	      			      +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values[velocities].value (j, q))
	      			       ) * scratch.fe_face_values.JxW(q);
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values[velocities].value (j, q))
				      + (scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (j, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		      else // scratch.mode_type==linear
	      			{
	      			  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
	      			    *( 
	      			      (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
	      			      +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
	      			       ) * scratch.fe_face_values.JxW(q);
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
				      + (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		    }
			  if (scratch.mode_type==state)
			    {
			      if (fem_properties.fluid_newton) 
				{
				  data.cell_rhs(i) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
				    *(
				      (u_star_side*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
				      ) * scratch.fe_face_values.JxW(q);
				}
			      data.cell_rhs(i) -= 0.5 * pow(1-fem_properties.fluid_theta,2) * physical_properties.rho_f 
				*( 
				  (u_old_side*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				   ) * scratch.fe_face_values.JxW(q);
			    }
	      		}
	      	    }
	      	}
	      if (scratch.mode_type==state)
		{
		  if (fluid_boundaries[cell->face(face_no)->boundary_indicator()]==Neumann)
		    {
		      if (physical_properties.simulation_type != 1)
			{
			  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
			    {
			      fluid_stress_values.set_time(time);
			      fluid_stress_values.vector_gradient(scratch.fe_face_values.quadrature_point(q),
								  stress_values);
			      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
				{
				  Tensor<2,dim,double> new_stresses;
				  // A TRANSPOSE IS TAKING PLACE HERE SINCE Deal.II has transposed gradient
				  new_stresses[0][0]=stress_values[0][0];
				  new_stresses[1][0]=stress_values[0][1];
				  new_stresses[1][1]=stress_values[1][1];
				  new_stresses[0][1]=stress_values[1][0];

				  data.cell_rhs(i) += fem_properties.fluid_theta*(new_stresses*scratch.fe_face_values.normal_vector(q) *
								   scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.JxW(q));
				}
			      fluid_stress_values.set_time(time-time_step);
			      fluid_stress_values.vector_gradient(scratch.fe_face_values.quadrature_point(q),
								  stress_values);
			      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
				{
				  Tensor<2,dim,double> new_stresses;
				  new_stresses[0][0]=stress_values[0][0];
				  new_stresses[1][0]=stress_values[1][0];
				  new_stresses[1][1]=stress_values[1][1];
				  new_stresses[0][1]=stress_values[0][1];
				  // data.cell_rhs(i) += (1-fem_properties.fluid_theta)*(scratch.fe_face_values[velocities].value (i, q)*
				  // 				       new_stresses*scratch.fe_face_values.normal_vector(q) *
				  // 				       scratch.fe_face_values.JxW(q));
				  data.cell_rhs(i) += (1-fem_properties.fluid_theta)*(new_stresses*scratch.fe_face_values.normal_vector(q) *
								       scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.JxW(q));
				}
			    }
			}
		      else
		      	{
			  // This gives sigma *dot normal boundary condition data from the function u_true_side_values
		      	  fluid_boundary_values_function.set_time(time - (1-fem_properties.fluid_theta)*time_step);
		      	  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
		      	    {
		      	      Tensor<1,dim,double> u_true_side;
		      	      if (physical_properties.simulation_type!=0 && physical_properties.simulation_type!=2) 
		      		{
		      		  fluid_boundary_values_function.vector_value(scratch.fe_face_values.quadrature_point(q),
		      							      u_true_side_values);
		      		  for (unsigned int d=0; d<dim; ++d)
		      		    {
		      		      u_true_side[d] = u_true_side_values(d);
		      		    }
		      		}
		      
		      	      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
		      		{
		      		  data.cell_rhs(i) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
		      		    *(
		      		      u_true_side*scratch.fe_face_values[velocities].value (i, q)
		      		      ) * scratch.fe_face_values.JxW(q);
		      		}
		      	    }
		      	}
		    }
		}
	    }
	  else if (fluid_boundaries[cell->face(face_no)->boundary_indicator()]==Interface)
	    {
	      scratch.fe_face_values.reinit (cell, face_no);

	      if (data.assemble_matrix)
	      if ((!physical_properties.moving_domain && physical_properties.navier_stokes) && physical_properties.stability_terms)
	      	{
	      	  scratch.fe_face_values.get_function_values (old_solution.block(0), old_solution_side_values);
	      	  scratch.fe_face_values.get_function_values (solution_star.block(0), u_star_side_values);
	      	  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
	      	    {
	      	      Tensor<1,dim,double> u_old_old_side, u_old_side, u_star_side;
	      	      for (unsigned int d=0; d<dim; ++d)
	      		{
	      		  u_old_old_side[d] = old_old_solution_side_values[q](d);
	      		  u_old_side[d] = old_solution_side_values[q](d);
	      		  u_star_side[d] = u_star_side_values[q](d);
	      		}
	      	      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
	      		{
	      		  for (unsigned int j=0; j<fluid_fe.dofs_per_cell; ++j)
	      		    {
	      		      if (scratch.mode_type==state)
	      			{
				  if (fem_properties.fluid_newton)
				    {
				      data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
					*( 
					  (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
					  +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
					   ) * scratch.fe_face_values.JxW(q);
				    }
				  else
				    {
				      data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
					*( 
					  (u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
					   ) * scratch.fe_face_values.JxW(q);
				    }
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
				      + (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		      else if (scratch.mode_type==adjoint) 
	      			{
	      			  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
	      			    *( 
	      			      (scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (j, q))
	      			      +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values[velocities].value (j, q))
	      			       ) * scratch.fe_face_values.JxW(q);
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values[velocities].value (j, q))
				      + (scratch.fe_face_values[velocities].value (i, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (j, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		      else // scratch.mode_type==linear
	      			{
	      			  data.cell_matrix(i,j) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
	      			    *( 
	      			      (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
	      			      +(u_star_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
	      			       ) * scratch.fe_face_values.JxW(q);
				  data.cell_matrix(i,j) += 0.5 * (1 - fem_properties.fluid_theta) * fem_properties.fluid_theta * physical_properties.rho_f 
				    *( 
				      (u_old_side*scratch.fe_face_values.normal_vector(q))*(scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values[velocities].value (i, q))
				      + (scratch.fe_face_values[velocities].value (j, q)*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				       ) * scratch.fe_face_values.JxW(q);
	      			}
	      		    }
			  if (scratch.mode_type==state)
			    {
			      if (fem_properties.fluid_newton) 
				{
				  data.cell_rhs(i) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
				    *(
				      (u_star_side*scratch.fe_face_values.normal_vector(q))*(u_star_side*scratch.fe_face_values[velocities].value (i, q))
				      ) * scratch.fe_face_values.JxW(q);
				}
			      data.cell_rhs(i) -= 0.5 * pow(1-fem_properties.fluid_theta,2) * physical_properties.rho_f 
				*( 
				  (u_old_side*scratch.fe_face_values.normal_vector(q))*(u_old_side*scratch.fe_face_values[velocities].value (i, q))
				   ) * scratch.fe_face_values.JxW(q);
			    }
	      		}
	      	    }
	      	}
	      if (scratch.mode_type==state)
		{
		  scratch.fe_face_values.reinit (cell, face_no);
		  scratch.fe_face_values.get_function_values (stress.block(0), g_stress_values);
		  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
		    {
		      Tensor<1,dim,double> g_stress;
		      for (unsigned int d=0; d<dim; ++d)
			g_stress[d] = g_stress_values[q](d);
		      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
			{
			  data.cell_rhs(i) += fem_properties.fluid_theta*(scratch.fe_face_values[velocities].value (i, q)*
						       g_stress * scratch.fe_face_values.JxW(q));
			}
		    }

		  scratch.fe_face_values.get_function_values (old_stress.block(0), g_stress_values);
		  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
		    {
		      Tensor<1,dim,double> g_stress;
		      for (unsigned int d=0; d<dim; ++d)
			g_stress[d] = g_stress_values[q](d);
		      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
			{
			  data.cell_rhs(i) += (1-fem_properties.fluid_theta)*(scratch.fe_face_values[velocities].value (i, q)*
							   g_stress * scratch.fe_face_values.JxW(q));
			}
		    }
		}
	      else if (scratch.mode_type==adjoint)
		{
		  scratch.fe_face_values.reinit (cell, face_no);
		  scratch.fe_face_values.get_function_values (rhs_for_adjoint.block(0), adjoint_rhs_values);

		  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
		    {
		      Tensor<1,dim,double> r;
		      for (unsigned int d=0; d<dim; ++d)
			r[d] = adjoint_rhs_values[q](d);
		      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
			{
			  data.cell_rhs(i) += fem_properties.fluid_theta*(scratch.fe_face_values[velocities].value (i, q)*
						       r * scratch.fe_face_values.JxW(q));
			}
		    }
		}
	      else // scratch.mode_type==linear
		{
		  scratch.fe_face_values.reinit (cell, face_no);
		  scratch.fe_face_values.get_function_values (rhs_for_linear.block(0), linear_rhs_values);

		  for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
		    {
		      Tensor<1,dim,double> h;
		      for (unsigned int d=0; d<dim; ++d)
			h[d] = linear_rhs_values[q](d);
		      for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
			{
			  data.cell_rhs(i) += fem_properties.fluid_theta*(scratch.fe_face_values[velocities].value (i, q)*
						       h * scratch.fe_face_values.JxW(q));
			}
		    }
		}
	    }
	  else if (fluid_boundaries[cell->face(face_no)->boundary_indicator()]==Dirichlet)
	    {
	      if (data.assemble_matrix)
	      if (physical_properties.navier_stokes && physical_properties.stability_terms)
	      	{
		  if (scratch.mode_type==state)
		    {
		      scratch.fe_face_values.reinit (cell, face_no);
		      for (unsigned int q=0; q<scratch.n_face_q_points; ++q)
			{
			  Tensor<1,dim,double> u_true_side;
			  if (physical_properties.simulation_type==0 || physical_properties.simulation_type==2) // assumes only time there would be nonzero dirichlet bc
			    {
			      fluid_boundary_values_function.set_time (time);
			      fluid_boundary_values_function.vector_value(scratch.fe_face_values.quadrature_point(q),
									  u_true_side_values);
			      for (unsigned int d=0; d<dim; ++d)
				{
				  u_true_side[d] = u_true_side_values(d);
				}
			    }
		      
			  for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
			    {
			      data.cell_rhs(i) += 0.5 * pow(fem_properties.fluid_theta,2) * physical_properties.rho_f 
				*(
				  (u_true_side*scratch.fe_face_values.normal_vector(q))*(u_true_side*scratch.fe_face_values[velocities].value (i, q))
				  ) * scratch.fe_face_values.JxW(q);
			    }
			}
		    }
	      	}
	    }
	}
    }
  cell->get_dof_indices (data.dof_indices);
}


template <int dim>
void FSIProblem<dim>::copy_local_fluid_to_global (const PerTaskData<dim>& data )
{
  // ConditionalOStream pcout(std::cout,Threads::this_thread_id()==0);//master_thread); 
  //TimerOutput timer (pcout, TimerOutput::summary,
  //		     TimerOutput::wall_times);
  //timer.enter_subsection ("Copy");
  // if (physical_properties.stability_terms)
  //   {
  //     if (data.assemble_matrix)
  // 	{
  // 	  for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
  // 	    for (unsigned int j=0; j<fluid_fe.dofs_per_cell; ++j)
  // 	      data.global_matrix->add (data.dof_indices[i], data.dof_indices[j], data.cell_matrix(i,j));
  // 	  for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
  // 	    (*data.global_rhs)(data.dof_indices[i]) += data.cell_rhs(i);
  // 	}
  //     else
  // 	{
  // 	  for (unsigned int i=0; i<fluid_fe.dofs_per_cell; ++i)
  // 	    (*data.global_rhs)(data.dof_indices[i]) += data.cell_rhs(i);
  // 	}
  //   }
  // else
    // {
      if (data.assemble_matrix)
	{
	  fluid_constraints.distribute_local_to_global (data.cell_matrix, data.cell_rhs,
  							data.dof_indices,
  							*data.global_matrix, *data.global_rhs);
	}
      else
	{
	  fluid_constraints.distribute_local_to_global (data.cell_rhs,
  							data.dof_indices,
  							*data.global_rhs);
	}
    // }
}

template <int dim>
void FSIProblem<dim>::assemble_fluid (Mode enum_, bool assemble_matrix)
{
  SparseMatrix<double> *fluid_matrix;
  Vector<double> *fluid_rhs;
  if (enum_==state)
    {
      fluid_matrix = &system_matrix.block(0,0);
      fluid_rhs = &system_rhs.block(0);
    }
  else if (enum_==adjoint)
    {
      fluid_matrix = &adjoint_matrix.block(0,0);
      fluid_rhs = &adjoint_rhs.block(0);
    }
  else
    {
      fluid_matrix = &linear_matrix.block(0,0);
      fluid_rhs = &linear_rhs.block(0);
    }

  if (assemble_matrix)
    {
      (*fluid_matrix) *= 0;
    }
  (*fluid_rhs) *= 0;

  Vector<double> temporary_vector(fluid_rhs->size());
  Vector<double> forcing_terms(fluid_rhs->size());

  QGauss<dim>   quadrature_formula(fem_properties.fluid_degree+2);
  FEValues<dim> fe_values (fluid_fe, quadrature_formula,
			   update_values    | update_gradients  |
			   update_quadrature_points | update_JxW_values);

  QGauss<dim-1> face_quadrature_formula(fem_properties.fluid_degree+2);
  FEFaceValues<dim> fe_face_values (fluid_fe, face_quadrature_formula,
				    update_values    | update_normal_vectors |
				    update_quadrature_points  | update_JxW_values);

  if (enum_==state)
    {
      temporary_vector *= 0;
      FluidRightHandSide<dim> rhs_function(physical_properties);
      rhs_function.set_time(time);
      VectorTools::create_right_hand_side(fluid_dof_handler,
					  QGauss<dim>(fluid_fe.degree+2),
					  rhs_function,
					  temporary_vector);
      forcing_terms = temporary_vector;
      forcing_terms *= fem_properties.fluid_theta;
      rhs_function.set_time(time - time_step);
      VectorTools::create_right_hand_side(fluid_dof_handler,
					  QGauss<dim>(fluid_fe.degree+2),
					  rhs_function,
					  temporary_vector);
      forcing_terms.add((1 - fem_properties.fluid_theta), temporary_vector);
      (*fluid_rhs) += forcing_terms;
    }

  ale_transform_fluid(); // Move the domain to the deformed ALE configuration

  //static int master_thread = Threads::this_thread_id();

  PerTaskData<dim> per_task_data(fluid_fe, fluid_matrix, fluid_rhs, assemble_matrix);
  FullScratchData<dim> scratch_data(fluid_fe, quadrature_formula, update_values | update_gradients | update_quadrature_points | update_JxW_values,
				     face_quadrature_formula, update_values | update_normal_vectors | update_quadrature_points  | update_JxW_values,
				     (unsigned int)enum_);//, vertices_quadrature_formula, update_values);
 
  // WorkStream::run (fluid_dof_handler.begin_active(),
  // 		   fluid_dof_handler.end(),
  // 		   boost::bind(&FSIProblem<dim>::assemble_fluid_matrix_on_one_cell,
  // 				   *this,
  // 			       // current_solution,
  // 				   boost::_1,
  // 				   boost::_2,
  // 			       std_cxx11::_3),
  // 			       //previous_time+time_step),
  // 		   std_cxx11::bind(&FSIProblem<dim>::copy_local_fluid_to_global,
  // 				   *this,
  // 				   std_cxx11::_1),
  // 		   per_task_data);


  WorkStream::run (fluid_dof_handler.begin_active(),
  		   fluid_dof_handler.end(),
  		   *this,
  		   &FSIProblem<dim>::assemble_fluid_matrix_on_one_cell,
  		   &FSIProblem<dim>::copy_local_fluid_to_global,
  		   scratch_data,
  		   per_task_data);

  // // TEMPORARY VISUALIZATION OF MOVED VERTICES
  // const std::string fluid_mesh_filename = "fluid-mesh" +
  //   Utilities::int_to_string (timestep_number, 3) +
  //   ".eps";
  // std::ofstream mesh_out (fluid_mesh_filename.c_str());
  // GridOut grid_out;
  // grid_out.write_eps (fluid_triangulation, mesh_out);
  // // 

  ref_transform_fluid(); // Move the domain back to reference configuration
}

template <int dim>
void FSIProblem<dim>::ale_transform_fluid()
{
  QTrapez<dim> vertices_quadrature_formula;
  FEValues<dim> fe_vertices_values (fluid_fe, vertices_quadrature_formula,
				    update_values);
  std::vector<Vector<double> > z_vertices(vertices_quadrature_formula.size(), Vector<double>(dim+1));
  std::vector<Vector<double> > z_old_vertices(vertices_quadrature_formula.size(), Vector<double>(dim+1));
  std::set<unsigned int> visited_vertices;
  typename DoFHandler<dim>::active_cell_iterator
    cell = fluid_dof_handler.begin_active(),
    endc = fluid_dof_handler.end();
  for (; cell!=endc; ++cell) {
    fe_vertices_values.reinit(cell);
    if (physical_properties.move_domain)
      {
	for (unsigned int i=0; i<GeometryInfo<dim>::vertices_per_cell; ++i)
	  {
	    if (visited_vertices.find(cell->vertex_index(i)) == visited_vertices.end())
	      {
		Point<2> &v = cell->vertex(i);
		fe_vertices_values.get_function_values(mesh_displacement_star.block(0), z_vertices);
		fe_vertices_values.get_function_values(mesh_displacement_star_old.block(0), z_old_vertices);
		for (unsigned int j=0; j<dim; ++j)
		  {
		    v(j) += z_vertices[i](j);
		  }
		visited_vertices.insert(cell->vertex_index(i));
	      }
	  }
      }
  }
}

template <int dim>
void FSIProblem<dim>::ref_transform_fluid()
{
  QTrapez<dim> vertices_quadrature_formula;
  FEValues<dim> fe_vertices_values (fluid_fe, vertices_quadrature_formula,
				    update_values);
  std::vector<Vector<double> > z_vertices(vertices_quadrature_formula.size(), Vector<double>(dim+1));
  std::vector<Vector<double> > z_old_vertices(vertices_quadrature_formula.size(), Vector<double>(dim+1));
  std::set<unsigned int> visited_vertices;
  typename DoFHandler<dim>::active_cell_iterator
    cell = fluid_dof_handler.begin_active(),
    endc = fluid_dof_handler.end();
  for (; cell!=endc; ++cell) {
    fe_vertices_values.reinit(cell);
    if (physical_properties.move_domain)
      {
  	for (unsigned int i=0; i<GeometryInfo<dim>::vertices_per_cell; ++i)
  	  {
  	    if (visited_vertices.find(cell->vertex_index(i)) == visited_vertices.end())
  	      {
  		Point<2> &v = cell->vertex(i);
  		fe_vertices_values.get_function_values(mesh_displacement_star.block(0), z_vertices);
  		fe_vertices_values.get_function_values(mesh_displacement_star_old.block(0), z_old_vertices);
  		for (unsigned int j=0; j<dim; ++j)
  		  {
  		    v(j) -= z_vertices[i](j);
  		  }
  		visited_vertices.insert(cell->vertex_index(i));
  	      }
  	  }
      }
  }
}



template void FSIProblem<2>::fluid_state_solve(unsigned int initialized_timestep_number);

template void FSIProblem<2>::assemble_fluid_matrix_on_one_cell (const DoFHandler<2>::active_cell_iterator& cell,
							     FullScratchData<2>& scratch,
							     PerTaskData<2>& data );

template void FSIProblem<2>::copy_local_fluid_to_global (const PerTaskData<2> &data);

template void FSIProblem<2>::assemble_fluid (Mode enum_, bool assemble_matrix);

template void FSIProblem<2>::ale_transform_fluid();

template void FSIProblem<2>::ref_transform_fluid();


// h           fluid.vel.L2   fluid.vel.H1   fluid.press.L2   structure.displ.L2   structure.displ.H1   structure.vel.L2
// 0.353553               -              -                -                    -                    -                  -
// 0.235702            3.57           2.37             2.15                 3.84                 3.04               3.15
// 0.157135            3.67           2.63             2.03                 3.76                 2.71               3.10
