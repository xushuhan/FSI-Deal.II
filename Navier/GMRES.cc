#include "FSI_Project.h"
#include "linear_maps.h"
#include <cmath>

/*
To Do: Actually update the stress before leaving when the error is sufficiently small.
 */
/*
%%%%%%%%%%%%%%  MATLAB PROGRAM FOR BICGSTAB %%%%%%%%%%%%%%
function [ c, s ] = rotmat( a, b )

%
% Compute the Givens rotation matrix parameters for a and b.
%
   if ( b == 0.0 ),
      c = 1.0;
      s = 0.0;
   elseif ( abs(b) > abs(a) ),
      temp = a / b;
      s = 1.0 / sqrt( 1.0 + temp^2 );
      c = temp * s;
   else
      temp = b / a;
      c = 1.0 / sqrt( 1.0 + temp^2 );
      s = temp * c;
   end

function [x, error, iter, flag] = GMRES( A, x, b, M, restrt, max_it, tol )

%  -- Iterative template routine --
%     Univ. of Tennessee and Oak Ridge National Laboratory
%     October 1, 1993
%     Details of this algorithm are described in "Templates for the
%     Solution of Linear Systems: Building Blocks for Iterative
%     Methods", Barrett, Berry, Chan, Demmel, Donato, Dongarra,
%     Eijkhout, Pozo, Romine, and van der Vorst, SIAM Publications,
%     1993. (ftp netlib2.cs.utk.edu; cd linalg; get templates.ps).
%
% [x, error, iter, flag] = gmres( A, x, b, M, restrt, max_it, tol )
%
% gmres.m solves the linear system Ax=b
% using the Generalized Minimal residual ( GMRESm ) method with restarts .
%
% input   A        REAL nonsymmetric positive definite matrix
%         x        REAL initial guess vector
%         b        REAL right hand side vector
%         M        REAL preconditioner matrix
%         restrt   INTEGER number of iterations between restarts
%         max_it   INTEGER maximum number of iterations
%         tol      REAL error tolerance
%
% output  x        REAL solution vector
%         error    REAL error norm
%         iter     INTEGER number of iterations performed
%         flag     INTEGER: 0 = solution found to tolerance
%                           1 = no convergence given max_it

   iter = 0;                                         % initialization
   flag = 0;

   bnrm2 = norm( b );
   if  ( bnrm2 == 0.0 ), bnrm2 = 1.0; end

   r = M \ ( b-A*x );
   error = norm( r ) / bnrm2;
   if ( error < tol ) return, end

   [n,n] = size(A);                                  % initialize workspace
   m = restrt;
   V(1:n,1:m+1) = zeros(n,m+1);
   H(1:m+1,1:m) = zeros(m+1,m);
   cs(1:m) = zeros(m,1);
   sn(1:m) = zeros(m,1);
   e1    = zeros(n,1);
   e1(1) = 1.0;

   for iter = 1:max_it,                              % begin iteration

      r = M \ ( b-A*x );
      V(:,1) = r / norm( r );
      s = norm( r )*e1;
      for i = 1:m,                                   % construct orthonormal
	 w = M \ (A*V(:,i));                         % basis using Gram-Schmidt
	 for k = 1:i,
	   H(k,i)= w'*V(:,k);
	   w = w - H(k,i)*V(:,k);
	 end
	 H(i+1,i) = norm( w );
	 V(:,i+1) = w / H(i+1,i);
	 for k = 1:i-1,                              % apply Givens rotation
            temp     =  cs(k)*H(k,i) + sn(k)*H(k+1,i);
            H(k+1,i) = -sn(k)*H(k,i) + cs(k)*H(k+1,i);
            H(k,i)   = temp;
	 end
	 [cs(i),sn(i)] = rotmat( H(i,i), H(i+1,i) ); % form i-th rotation matrix
         temp   = cs(i)*s(i);                        % approximate residual norm
         s(i+1) = -sn(i)*s(i);
	 s(i)   = temp;
         H(i,i) = cs(i)*H(i,i) + sn(i)*H(i+1,i);
         H(i+1,i) = 0.0;
	 error  = abs(s(i+1)) / bnrm2;
	 if ( error <= tol ),                        % update approximation
	    y = H(1:i,1:i) \ s(1:i);                 % and exit
            x = x + V(:,1:i)*y;
	    break;
	 end
      end

      if ( error <= tol ), break, end
      y = H(1:m,1:m) \ s(1:m);
      x = x + V(:,1:m)*y;                            % update approximation
      r = M \ ( b-A*x )                              % compute residual
      s(i+1) = norm(r);
      error = s(i+1) / bnrm2;                        % check convergence
      if ( error <= tol ), break, end;
   end

   if ( error > tol ) flag = 1; end;                 % converged

% END of gmres.m
*/

void rotmat(const double a, const double b, double &c, double &s ) {
  //
  // Compute the Givens rotation matrix parameters for a and b.
  //
  // std::cout << "a: " << a << " b: " << b << " c: " << c << " s: " << s << std::endl;
  // std::cout << std::fabs(a) << " " << std::fabs(b) << std::endl;
  // std::cout << pow(1.1,2) << std::endl;
  if ( b == 0.0 ) {
    c = 1.0;
    s = 0.0;
    // std::cout << "first case called." << std::endl;
  } else if (std::fabs(b) > std::fabs(a)) {
    double temp = a / b;
    s = 1.0 / std::sqrt( 1.0 + pow(temp,2) );
    c = temp * s;
    // std::cout << "temp: " << temp << " s: " << s << " c: " << c << std::endl;
  } else {
    double temp = b / a;
    c = 1.0 / std::sqrt( 1.0 + pow(temp,2) );
    s = temp * c;
  }
  
}

template <int dim>
unsigned int FSIProblem<dim>::optimization_GMRES (unsigned int &total_solves, const unsigned int initial_timestep_number, const bool random_initial_guess, const unsigned int max_iterations)
{
  unsigned int restrt = 3;
  unsigned int iter = 0;  //                                       % initialization
  unsigned int flag = 0;

  BlockVector<double> b = rhs_for_adjoint; // just to initialize it
  BlockVector<double> r = rhs_for_adjoint; // just to initialize it
  BlockVector<double> v = rhs_for_adjoint; // just to initialize it
  v.block(0)*=0;


  const bool debug = false;

  // bnrm2 = norm( b );
  if (fem_properties.adjoint_type==1)
    {
      // b = -u + [n^n-n^n-1]/dt	       
      tmp=0;
      b=0;
      transfer_interface_dofs(solution,b,1,0,Displacement);
      b.block(0)*=1./time_step;
      transfer_interface_dofs(old_solution,tmp,1,0,Displacement);
      b.block(0).add(-1./time_step,tmp.block(0));
      tmp=0;
      transfer_interface_dofs(solution,tmp,0,0);
      b.block(0)-=tmp.block(0);
    }
  else
    {
      // b = -u + v^	       
      tmp=0;
      b=0;
      transfer_interface_dofs(solution,b,1,0,Velocity);
      tmp=0;
      transfer_interface_dofs(solution,tmp,0,0);
      b.block(0)-=tmp.block(0);
    }
  //premultiplier.block(0) = b.block(0);
  double bnrm2 = interface_norm(b.block(0));

  // This gives the initial guess x_0
  // if (random_initial_guess) {
  //   // Generate a random vector
  //   for (Vector<double>::iterator it=tmp.block(0).begin(); it!=tmp.block(0).end(); ++it) *it = ((double)std::rand() / (double)(RAND_MAX)) * std::max(physical_properties.rho_f,physical_properties.rho_s);// * std::pow(bnrm2,2);
  // } else {
    for (Vector<double>::iterator it=tmp.block(0).begin(); it!=tmp.block(0).end(); ++it) *it = std::max(physical_properties.rho_f,physical_properties.rho_s);// * std::pow(bnrm2,2);
    //}
  rhs_for_linear_h=0;
  transfer_interface_dofs(tmp,rhs_for_linear_h,0,0);
  transfer_interface_dofs(rhs_for_linear_h,rhs_for_linear_h,0,1,Displacement);
  rhs_for_linear_h.block(1) *= -1;   // copy, negate

  if (bnrm2 == 0.0) bnrm2 = 1.0;
  std::cout << "bnrm2: " << bnrm2 << std::endl;

  //  r = M \ ( b-A*x );
  // get linearized variables
  rhs_for_linear = rhs_for_linear_h;
  // timer.enter_subsection ("Assemble");
  Threads::Task<void> s_assembly = Threads::new_task(&FSIProblem<dim>::assemble_structure, *this, linear, true);
  Threads::Task<void> f_assembly = Threads::new_task(&FSIProblem<dim>::assemble_fluid, *this, linear, true);	      
  f_assembly.join();
  dirichlet_boundaries((System)0,linear);
  s_assembly.join();
  dirichlet_boundaries((System)1,linear);
  // timer.leave_subsection ();

  // timer.enter_subsection ("Linear Solve");
  if (timestep_number==initial_timestep_number) {
    linear_solver[0].initialize(linear_matrix.block(0,0));
    linear_solver[1].initialize(linear_matrix.block(1,1));
    Threads::Task<void> f_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[0],0,linear);
    Threads::Task<void> s_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[1],1,linear);
    f_solve.join();
    s_solve.join();
  } else {
    Threads::Task<void> f_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[0], linear_matrix.block(0,0));
    Threads::Task<void> s_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[1], linear_matrix.block(1,1));
    f_factor.join();
    Threads::Task<void> f_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[0],0,linear);
    s_factor.join();
    Threads::Task<void> s_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[1],1,linear);
    f_solve.join();
    s_solve.join();
  }
  // timer.leave_subsection ();
  total_solves += 2;
  if (fem_properties.adjoint_type==1)
    {
      // -Ax = -w^n + phi^n/dt
      tmp=0;tmp2=0;
      transfer_interface_dofs(linear_solution,tmp,1,0,Displacement);
      tmp.block(0)*=1./time_step;
      transfer_interface_dofs(linear_solution,tmp2,0,0);
      tmp.block(0)-=tmp2.block(0);
    }
  else
    {
      // -Ax = -w^n + phi_dot^n
      tmp=0;tmp2=0;
      transfer_interface_dofs(linear_solution,tmp,1,0,Velocity);
      transfer_interface_dofs(linear_solution,tmp2,0,0);
      tmp.block(0)-=tmp2.block(0);
    }

  tmp2*=0;
  std::cout << "Original norm: " << tmp.block(0).l2_norm() << std::endl;
  LinearMap::Linearized_Operator<dim> A(this);
  LinearMap::NeumannVector<dim> input_vector(rhs_for_linear_h.block(0), this);
  LinearMap::NeumannVector<dim> output_vector(tmp2.block(0), this);
  A.vmult(output_vector, input_vector);
  tmp.block(0).add(-1.0, output_vector);
  std::cout << "Difference: " << tmp.block(0).l2_norm() << std::endl;
  

  // r^0 = b - Ax
  r.block(0)  = b.block(0);
  r.block(0) *= -1;
  r.block(0) -= tmp.block(0);
  //r = b + tmp;
  // tmp.block(0) = 0;
  // linear_matrix.block(0,0).vmult(tmp.block(0), r.block(0));
  // r.block(0)=tmp.block(0);
  
  if (debug) std::cout << "r_norm_initial: " << r.block(0).l2_norm() << std::endl;

  //premultiplier.block(0)=r.block(0);
  double error = interface_norm(r.block(0))/bnrm2;
  
  if (error < fem_properties.cg_tolerance) {
    std::cout << "Error sufficiently small before running algorithm." << std::endl;
    return 0;
  }
  unsigned int n = stress.block(0).size();
  // [n,n] = size(A);                                  % initialize workspace
  unsigned int m = restrt;
  //std::vector<std::vector< double > > V(n,std::vector<double>(m+1));
  FullMatrix<double> V(n,m+1);
  // V(1:n,1:m+1) = zeros(n,m+1);
  FullMatrix<double> H(m+1,m);	
  //H(1:m+1,1:m) = zeros(m+1,m);
  Vector<double> cs(m);
  // cs(1:m) = zeros(m,1);
  Vector<double> sn(m);
  // sn(1:m) = zeros(m,1);
  Vector<double> e1(m+1);
  //e1    = zeros(n,1);
  e1[0] = 1.0;
  // e1(1) = 1.0;
  //std::cout << "n,m: " << n << "," << m << std::endl;
    
  for (unsigned int iter = 0; iter < max_iterations; iter++) {
    //  r = M \ ( b-A*x );
    // get linearized variables
    rhs_for_linear = rhs_for_linear_h;
    // timer.enter_subsection ("Assemble");
    Threads::Task<void> s_assembly = Threads::new_task(&FSIProblem<dim>::assemble_structure, *this, linear, false);
    Threads::Task<void> f_assembly = Threads::new_task(&FSIProblem<dim>::assemble_fluid, *this, linear, false);	    
    f_assembly.join();
    dirichlet_boundaries((System)0,linear);
    s_assembly.join();
    dirichlet_boundaries((System)1,linear);
    // timer.leave_subsection ();
    // timer.enter_subsection ("Linear Solve");
    Threads::Task<void> f_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[0], linear_matrix.block(0,0));
    Threads::Task<void> s_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[1], linear_matrix.block(1,1));
    f_factor.join();
    Threads::Task<void> f_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[0],0,linear);
    s_factor.join();
    Threads::Task<void> s_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[1],1,linear);
    f_solve.join();
    s_solve.join();
    // timer.leave_subsection ();
    total_solves += 2;
    if (fem_properties.adjoint_type==1)
      {
	// -Ax = -w^n + phi^n/dt
	tmp=0;tmp2=0;
	transfer_interface_dofs(linear_solution,tmp,1,0,Displacement);
	tmp.block(0)*=1./time_step;
	transfer_interface_dofs(linear_solution,tmp2,0,0);
	tmp.block(0)-=tmp2.block(0);
      }
    else
      {
	// -Ax = -w^n + phi_dot^n
	tmp=0;tmp2=0;
	transfer_interface_dofs(linear_solution,tmp,1,0,Velocity);
	transfer_interface_dofs(linear_solution,tmp2,0,0);
	tmp.block(0)-=tmp2.block(0);
      }
    // r^0 = b - Ax
    r.block(0)  = b.block(0);
    r.block(0) *= -1;
    r.block(0) -= tmp.block(0);
    // added
    // tmp.block(0) = 0;
    // linear_matrix.block(0,0).vmult(tmp.block(0), r.block(0));
    // r.block(0)=tmp.block(0);
    // r = b.block(0) - M \ ( b-A*x );
    //premultiplier = r.block(0);
    double r_norm = interface_norm(r.block(0));
    std::cout << "inner iter r_norm: " << r_norm << std::endl;
    for (unsigned int l=0; l<n; l++)
      V.set(l,0,r.block(0)[l]/r_norm);
    // V(:,1) = r / norm( r );
    Vector<double> s = e1;
    s *= r_norm;
    // s = norm( r )*e1;
    int break_iter=-1;
    for (unsigned int i=0; i<m; i++) {
      // construct orthonormal basis using Gram-Schmidt
      for (unsigned int l=0; l<n; l++)
	v.block(0)[l] = V(l,i);
      transfer_interface_dofs(v,v,0,1,Displacement);
      v.block(1) *= -1;
      rhs_for_linear = v;
      // timer.enter_subsection ("Assemble");
      s_assembly = Threads::new_task(&FSIProblem<dim>::assemble_structure, *this, linear, false);
      f_assembly = Threads::new_task(&FSIProblem<dim>::assemble_fluid, *this, linear, false);	      
      f_assembly.join();
      dirichlet_boundaries((System)0,linear);
      s_assembly.join();
      dirichlet_boundaries((System)1,linear);
      // timer.leave_subsection ();

      // timer.enter_subsection ("Linear Solve");
      f_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[0], linear_matrix.block(0,0));
      s_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[1], linear_matrix.block(1,1));
      f_factor.join();
      f_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[0],0,linear);
      s_factor.join();
      s_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[1],1,linear);
      f_solve.join();
      s_solve.join();
      // timer.leave_subsection ();
      total_solves += 2;
      if (fem_properties.adjoint_type==1)
	{
	  // -Ax = -w^n + phi^n/dt
	  tmp=0;tmp2=0;
	  transfer_interface_dofs(linear_solution,tmp,1,0,Displacement);
	  tmp.block(0)*=1./time_step;
	  transfer_interface_dofs(linear_solution,tmp2,0,0);
	  tmp.block(0)-=tmp2.block(0);
	}
      else
	{
	  // -Ax = -w^n + phi_dot^n
	  tmp=0;tmp2=0;
	  transfer_interface_dofs(linear_solution,tmp,1,0,Velocity);
	  transfer_interface_dofs(linear_solution,tmp2,0,0);
	  tmp.block(0)-=tmp2.block(0);
	}
      BlockVector<double> w = tmp; 
      //w.block(0) *= -1;
      // added
      // tmp.block(0) = 0;
      // linear_matrix.block(0,0).vmult(tmp.block(0), w.block(0));
      // w.block(0)=tmp.block(0);

      // NOTE HERE: if tmp is usually -Ax, then w = -tmp since w=Av
      // w = M \ (A*V(:,i)); 
      for (int k=0; k<=(int)i; k++) {
	H.set(k,i,0.0);
	for (unsigned int l=0; l<n; l++) {
	  H(k,i)+=w.block(0)[l]*V(l,k);
	  // H[k,i] = transpose(w)*V(:,k);
	    
	}
	for (unsigned int l=0; l<n; l++) {
	  w.block(0)[l] -= H(k,i)*V(l,k);
	  // w - H(k,i)*V(:,k);
	}
      }
      //premultiplier.block(0) = w.block(0);
      double w_norm = interface_norm(w.block(0));
      H.set(i+1,i,w_norm);
      //std::cout << "w_norm: " << w_norm << std::endl;
      // H(i+1,i) = norm( w );
      for (unsigned int l=0; l<n; l++) {
	V.set(l,i+1,w.block(0)[l] / H(i+1,i));
      }
      // V(:,i+1) = w / H(i+1,i);
      double temp = 0;
      // i is an unsigned int, so i-1 is not what you would expect (unless you are paying attention)
      for (int k=0; k<=((int)i-1); k++) { //                            % apply Givens rotation
	temp     =  cs[k]*H(k,i) + sn[k]*H(k+1,i);
	H.set(k+1,i, -sn[k]*H(k,i) + cs[k]*H(k+1,i));
	H.set(k,i, temp);
      }
      rotmat(H(i,i), H(i+1,i), cs[i], sn[i]);
      // std::cout << "cs[i] sn[i]" << cs[i] << "," << sn[i] << std::endl;
      // [cs(i),sn(i)] = rotmat( H(i,i), H(i+1,i) ); % form i-th rotation matrix
      temp   = cs[i]*s[i]; //                       % approximate residual norm
      s[i+1] = -sn[i]*s[i];
      //std::cout << sn[i] << std::endl;
      s[i]   = temp;
      // std::cout << "approx resid. norm: " << temp << std::endl;
      H.set(i,i, cs[i]*H(i,i) + sn[i]*H(i+1,i));
      H.set(i+1,i, 0.0);
      error  = std::fabs(s[i+1]) / bnrm2;
      if (debug) std::cout << "error: " << error << std::endl;
      // *****************************************************
      // if ( error <= fem_properties.cg_tolerance ) {   //                     % update approximation
      // //	std::cout << "Error less than cg tolerance in iter i: " << i << std::endl;
      // 	unsigned int H_size = i+1;
      // 	FullMatrix<double> H_sub(H_size,H_size);
      // 	H_sub.fill(H);
      // 	Vector<double> y(H_size);
      // 	for (unsigned int l=0; l<H_size; l++) {
      // 	  y[l] = s[l];
      // 	}
      // 	H_sub.backward(y,y);
      // 	for (unsigned int l=0; l<n; l++) 
      // 	  for (unsigned int j=0; j<H_size; j++)
      // 	    rhs_for_linear_h.block(0)[l] = rhs_for_linear_h.block(0)[l] + V(l,j)*y[j]; 

      // 	// an update to the stress needs added here
      // 	if (debug) std::cout << "Left algorithm on an earlier i with error: " << error << std::endl;
      // 	// update stress
      // 	stress.block(0).add(1.0, rhs_for_linear_h.block(0));
      // 	tmp=0;
      // 	transfer_interface_dofs(stress,tmp,0,0);
      // 	transfer_interface_dofs(stress,tmp,1,1,Displacement);
      // 	stress=0;
      // 	transfer_interface_dofs(tmp,stress,0,0);
      // 	transfer_interface_dofs(tmp,stress,1,1,Displacement);

      // 	transfer_interface_dofs(stress,stress,0,1,Displacement);
      // 	return 0;
      // }
    }

    if (debug) std::cout << "Completed up to a restart." << std::endl;
    //if ( error <= fem_properties.cg_tolerance ) {std::cout << "broke loop" << std::endl;break;}
    unsigned int H_size = m;
    FullMatrix<double> H_sub(H_size,H_size);
    H_sub.fill(H);
    //H_sub.print_formatted(std::cout);
    Vector<double> y(H_size);
    for (unsigned int l=0; l<H_size; l++) {
      y[l] = s[l];
    }
    H_sub.backward(y,y);
    // y = H(1:m,1:m) \ s(1:m);
    for (unsigned int l=0; l<n; l++) 
      for (unsigned int j=0; j<H_size; j++)
	rhs_for_linear_h.block(0)[l] = rhs_for_linear_h.block(0)[l] + V(l,j)*y[j]; 
    // x = x + V(:,1:m)*y; //                           % update approximation
    //  r = M \ ( b-A*x );
    // get linearized variables
    transfer_interface_dofs(rhs_for_linear_h,rhs_for_linear_h,0,1,Displacement);
    rhs_for_linear_h.block(1) *= -1;
    rhs_for_linear = rhs_for_linear_h;
    // timer.enter_subsection ("Assemble");
    s_assembly = Threads::new_task(&FSIProblem<dim>::assemble_structure, *this, linear, false);
    f_assembly = Threads::new_task(&FSIProblem<dim>::assemble_fluid, *this, linear, false);	      
    f_assembly.join();
    dirichlet_boundaries((System)0,linear);
    s_assembly.join();
    dirichlet_boundaries((System)1,linear);
    // timer.leave_subsection ();

    // timer.enter_subsection ("Linear Solve");
    f_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[0], linear_matrix.block(0,0));
    s_factor = Threads::new_task(&SparseDirectUMFPACK::factorize<SparseMatrix<double> >,linear_solver[1], linear_matrix.block(1,1));
    f_factor.join();
    f_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[0],0,linear);
    s_factor.join();
    s_solve = Threads::new_task(&FSIProblem<dim>::solve,*this,linear_solver[1],1,linear);
    f_solve.join();
    s_solve.join();
    // timer.leave_subsection ();
    total_solves += 2;
    if (fem_properties.adjoint_type==1)
      {
	// -Ax = -w^n + phi^n/dt
	tmp=0;tmp2=0;
	transfer_interface_dofs(linear_solution,tmp,1,0,Displacement);
	tmp.block(0)*=1./time_step;
	transfer_interface_dofs(linear_solution,tmp2,0,0);
	tmp.block(0)-=tmp2.block(0);
      }
    else
      {
	// -Ax = -w^n + phi_dot^n
	tmp=0;tmp2=0;
	transfer_interface_dofs(linear_solution,tmp,1,0,Velocity);
	transfer_interface_dofs(linear_solution,tmp2,0,0);
	tmp.block(0)-=tmp2.block(0);
      }
    // r^0 = b - Ax
    r.block(0)  = b.block(0);
    r.block(0) *= -1;
    r.block(0) -= tmp.block(0);
    // added
    // tmp.block(0) = 0;
    // linear_matrix.block(0,0).vmult(tmp.block(0), r.block(0));
    // r.block(0)=tmp.block(0);
    //r = M \ ( b-A*x );  //                            % compute residual
    // ***************************************************
    //premultiplier = r.block(0);
    // if (break_iter >= 0)
    //   s[break_iter+1] = interface_norm(r.block(0));
    // else
    //std::cout << "Previous s[H_size] " << s[H_size] << std::endl;
    s[H_size] = interface_norm(r.block(0));
    //std::cout << "New s[H_size] " << s[H_size] << std::endl;
    //s(i+1) = norm(r);
    error = s[H_size] / bnrm2; //   
    //error = s(i+1) / bnrm2; // % check convergence
    if ( error <= fem_properties.cg_tolerance ) {
      // update stress
      stress.block(0).add(1.0, rhs_for_linear_h.block(0));
      tmp=0;
      transfer_interface_dofs(stress,tmp,0,0);
      transfer_interface_dofs(stress,tmp,1,1,Displacement);
      stress=0;
      transfer_interface_dofs(tmp,stress,0,0);
      transfer_interface_dofs(tmp,stress,1,1,Displacement);

      transfer_interface_dofs(stress,stress,0,1,Displacement);
      return 0; // success
      std::cout << "Broke out of iter loop." << std::endl; break;
    }
    std::cout << "error: " << error << std::endl;
  }
    
  return 1;
}

template unsigned int FSIProblem<2>::optimization_GMRES(unsigned int &total_solves, const unsigned int initial_timestep_number, const bool random_initial_guess, const unsigned int max_iterations);
