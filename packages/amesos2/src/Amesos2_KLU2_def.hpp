// @HEADER
//
// ***********************************************************************
//
//           Amesos2: Templated Direct Sparse Solver Package
//                  Copyright 2011 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ***********************************************************************
//
// @HEADER

/**
   \file   Amesos2_KLU2_def.hpp
   \author Siva Rajamanickam <srajama@sandia.gov>

   \brief  Definitions for the Amesos2 KLU2 solver interface
*/


#ifndef AMESOS2_KLU2_DEF_HPP
#define AMESOS2_KLU2_DEF_HPP

#include <Teuchos_Tuple.hpp>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_StandardParameterEntryValidators.hpp>

#include "Amesos2_SolverCore_def.hpp"
#include "Amesos2_KLU2_decl.hpp"

namespace Amesos2 {


template <class Matrix, class Vector>
KLU2<Matrix,Vector>::KLU2(
  Teuchos::RCP<const Matrix> A,
  Teuchos::RCP<Vector>       X,
  Teuchos::RCP<const Vector> B )
  : SolverCore<Amesos2::KLU2,Matrix,Vector>(A, X, B)
  , nzvals_()                   // initialize to empty arrays
  , rowind_()
  , colptr_()
  , transFlag_(0)
  , is_contiguous_(true)
{
  ::KLU2::klu_defaults<slu_type, local_ordinal_type> (&(data_.common_)) ;
  data_.symbolic_ = NULL;
  data_.numeric_ = NULL;

  // Override some default options
  // TODO: use data_ here to init
}


template <class Matrix, class Vector>
KLU2<Matrix,Vector>::~KLU2( )
{
  /* Free KLU2 data_types
   * - Matrices
   * - Vectors
   * - Other data
   */
  if (data_.symbolic_ != NULL)
      ::KLU2::klu_free_symbolic<slu_type, local_ordinal_type>
                         (&(data_.symbolic_), &(data_.common_)) ;
  if (data_.numeric_ != NULL)
      ::KLU2::klu_free_numeric<slu_type, local_ordinal_type>
                         (&(data_.numeric_), &(data_.common_)) ;

  // Storage is initialized in numericFactorization_impl()
  //if ( data_.A.Store != NULL ){
      // destoy
  //}

  // only root allocated these SuperMatrices.
  //if ( data_.L.Store != NULL ){       // will only be true for this->root_
      // destroy ..
  //}
}

template<class Matrix, class Vector>
int
KLU2<Matrix,Vector>::preOrdering_impl()
{
  /* TODO: Define what it means for KLU2
   */
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor preOrderTimer(this->timers_.preOrderTime_);
#endif

  return(0);
}


template <class Matrix, class Vector>
int
KLU2<Matrix,Vector>::symbolicFactorization_impl()
{
  if (data_.symbolic_ != NULL)
      ::KLU2::klu_free_symbolic<slu_type, local_ordinal_type>
                         (&(data_.symbolic_), &(data_.common_)) ;
  data_.symbolic_ = ::KLU2::klu_analyze<slu_type, local_ordinal_type>
                ((local_ordinal_type)this->globalNumCols_, colptr_.getRawPtr(),
                 rowind_.getRawPtr(), &(data_.common_)) ;

  return(0);
}


template <class Matrix, class Vector>
int
KLU2<Matrix,Vector>::numericFactorization_impl()
{
  using Teuchos::as;

  // Cleanup old L and U matrices if we are not reusing a symbolic
  // factorization.  Stores and other data will be allocated in gstrf.
  // Only rank 0 has valid pointers, TODO: for KLU2


  int info = 0;
  if ( this->root_ ){

    { // Do factorization
#ifdef HAVE_AMESOS2_TIMERS
      Teuchos::TimeMonitor numFactTimer(this->timers_.numFactTime_);
#endif

#ifdef HAVE_AMESOS2_VERBOSE_DEBUG
      std::cout << "KLU2:: Before numeric factorization" << std::endl;
      std::cout << "nzvals_ : " << nzvals_.toString() << std::endl;
      std::cout << "rowind_ : " << rowind_.toString() << std::endl;
      std::cout << "colptr_ : " << colptr_.toString() << std::endl;
#endif

    if (data_.numeric_ != NULL)
      ::KLU2::klu_free_numeric<slu_type, local_ordinal_type>
                         (&(data_.numeric_), &(data_.common_)) ;
      data_.numeric_ = ::KLU2::klu_factor<slu_type, local_ordinal_type>
                  (colptr_.getRawPtr(), rowind_.getRawPtr(), nzvals_.getRawPtr(),
                  data_.symbolic_, &(data_.common_)) ;

      // This is set after numeric factorization complete as pivoting can be used;
      // In this case, a discrepancy between symbolic and numeric nnz total can occur.
      this->setNnzLU( as<size_t>((data_.numeric_)->lnz) + as<size_t>((data_.numeric_)->unz) );
    }

  }

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->matrixA_->getComm()), 0, &info);

  //global_size_type info_st = as<global_size_type>(info); // unused
  /* TODO : Proper error messages
  TEUCHOS_TEST_FOR_EXCEPTION( (info_st > 0) && (info_st <= this->globalNumCols_),
    std::runtime_error,
    "Factorization complete, but matrix is singular. Division by zero eminent");
  TEUCHOS_TEST_FOR_EXCEPTION( (info_st > 0) && (info_st > this->globalNumCols_),
    std::runtime_error,
    "Memory allocation failure in KLU2 factorization");*/

  //data_.options.Fact = SLU::FACTORED;
  //same_symbolic_ = true;

  return(info);
}


template <class Matrix, class Vector>
int
KLU2<Matrix,Vector>::solve_impl(
 const Teuchos::Ptr<MultiVecAdapter<Vector> >  X,
 const Teuchos::Ptr<const MultiVecAdapter<Vector> > B) const
{
  using Teuchos::as;

  const global_size_type ld_rhs = this->root_ ? X->getGlobalLength() : 0;
  const size_t nrhs = X->getGlobalNumVectors();

  const size_t val_store_size = as<size_t>(ld_rhs * nrhs);
  Teuchos::Array<slu_type> bValues(val_store_size);

  {                             // Get values from RHS B
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor mvConvTimer(this->timers_.vecConvTime_);
    Teuchos::TimeMonitor redistTimer( this->timers_.vecRedistTime_ );
#endif
    if ( is_contiguous_ == true ) {
      Util::get_1d_copy_helper<MultiVecAdapter<Vector>,
        slu_type>::do_get(B, bValues(),
            as<size_t>(ld_rhs),
            ROOTED, this->rowIndexBase_);
    }
    else {
      Util::get_1d_copy_helper<MultiVecAdapter<Vector>,
        slu_type>::do_get(B, bValues(),
            as<size_t>(ld_rhs),
            CONTIGUOUS_AND_ROOTED, this->rowIndexBase_);
    }
  }


  int ierr = 0; // returned error code

  if ( this->root_ ) {

    //local_ordinal_type i_ld_rhs = as<local_ordinal_type>(ld_rhs);

    {                           // Do solve!
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor solveTimer(this->timers_.solveTime_);
#endif
    if (transFlag_ == 0)
    {
      ::KLU2::klu_solve<slu_type, local_ordinal_type>
                  (data_.symbolic_, data_.numeric_,
                  (local_ordinal_type)this->globalNumCols_,
                  (local_ordinal_type)nrhs,
                  bValues.getRawPtr(),  &(data_.common_)) ;
    }
    else
    {
      ::KLU2::klu_tsolve<slu_type, local_ordinal_type>
                  (data_.symbolic_, data_.numeric_,
                  (local_ordinal_type)this->globalNumCols_,
                  (local_ordinal_type)nrhs,
                  bValues.getRawPtr(),  &(data_.common_)) ;
    }

    }

  }

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->getComm()), 0, &ierr);

  // global_size_type ierr_st = as<global_size_type>(ierr); // unused
  // TODO
  //TEUCHOS_TEST_FOR_EXCEPTION( ierr < 0,
                      //std::invalid_argument,
                      //"Argument " << -ierr << " to KLU2 xgssvx had illegal value" );
  //TEUCHOS_TEST_FOR_EXCEPTION( ierr > 0 && ierr_st <= this->globalNumCols_,
                      //std::runtime_error,
                      //"Factorization complete, but U is exactly singular" );
  //TEUCHOS_TEST_FOR_EXCEPTION( ierr > 0 && ierr_st > this->globalNumCols_ + 1,
                      //std::runtime_error,
                      //"KLU2 allocated " << ierr - this->globalNumCols_ << " bytes of "
                      //"memory before allocation failure occured." );

  /* Update X's global values */
  {
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor redistTimer(this->timers_.vecRedistTime_);
#endif

    if ( is_contiguous_ == true ) {
      Util::put_1d_data_helper<
        MultiVecAdapter<Vector>,slu_type>::do_put(X, bValues(),
            as<size_t>(ld_rhs),
            ROOTED);
    }
    else {
      Util::put_1d_data_helper<
        MultiVecAdapter<Vector>,slu_type>::do_put(X, bValues(),
            as<size_t>(ld_rhs),
            CONTIGUOUS_AND_ROOTED);
    }
  }


  return(ierr);
}


template <class Matrix, class Vector>
bool
KLU2<Matrix,Vector>::matrixShapeOK_impl() const
{
  // The KLU2 factorization routines can handle square as well as
  // rectangular matrices, but KLU2 can only apply the solve routines to
  // square matrices, so we check the matrix for squareness.
  return( this->matrixA_->getGlobalNumRows() == this->matrixA_->getGlobalNumCols() );
}


template <class Matrix, class Vector>
void
KLU2<Matrix,Vector>::setParameters_impl(const Teuchos::RCP<Teuchos::ParameterList> & parameterList )
{
  using Teuchos::RCP;
  using Teuchos::getIntegralValue;
  using Teuchos::ParameterEntryValidator;

  RCP<const Teuchos::ParameterList> valid_params = getValidParameters_impl();

  transFlag_ = this->control_.useTranspose_ ? 1: 0;
  // The KLU2 transpose option can override the Amesos2 option
  if( parameterList->isParameter("Trans") ){
    RCP<const ParameterEntryValidator> trans_validator = valid_params->getEntry("Trans").validator();
    parameterList->getEntry("Trans").setValidator(trans_validator);

    transFlag_ = getIntegralValue<int>(*parameterList, "Trans");
  }

  if( parameterList->isParameter("IsContiguous") ){
//    RCP<const ParameterEntryValidator> trans_validator = valid_params->getEntry("IsContiguous").validator();
//    parameterList->getEntry("IsContiguous").setValidator(trans_validator);
    is_contiguous_ = parameterList->get<bool>("IsContiguous");
  }
}


template <class Matrix, class Vector>
Teuchos::RCP<const Teuchos::ParameterList>
KLU2<Matrix,Vector>::getValidParameters_impl() const
{
  using std::string;
  using Teuchos::tuple;
  using Teuchos::ParameterList;
  using Teuchos::setStringToIntegralParameter;

  static Teuchos::RCP<const Teuchos::ParameterList> valid_params;

  if( is_null(valid_params) )
  {
    Teuchos::RCP<Teuchos::ParameterList> pl = Teuchos::parameterList();

    pl->set("Equil", true, "Whether to equilibrate the system before solve, does nothing now");
    pl->set("IsContiguous", true, "Whether GIDs contiguous");

    setStringToIntegralParameter<int>("Trans", "NOTRANS",
                                      "Solve for the transpose system or not",
                                      tuple<string>("NOTRANS","TRANS","CONJ"),
                                      tuple<string>("Solve with transpose",
                                                    "Do not solve with transpose",
                                                    "Solve with the conjugate transpose"),
                                      tuple<int>(0, 1, 2),
                                      pl.getRawPtr());
    valid_params = pl;
  }

  return valid_params;
}


template <class Matrix, class Vector>
bool
KLU2<Matrix,Vector>::loadA_impl(EPhase current_phase)
{
  using Teuchos::as;

  if(current_phase == SOLVE)return(false);

#ifdef HAVE_AMESOS2_TIMERS
  Teuchos::TimeMonitor convTimer(this->timers_.mtxConvTime_);
#endif

  // Only the root image needs storage allocated
  if( this->root_ ){
    nzvals_.resize(this->globalNumNonZeros_);
    rowind_.resize(this->globalNumNonZeros_);
    colptr_.resize(this->globalNumCols_ + 1);
  }

  local_ordinal_type nnz_ret = 0;
  {
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor mtxRedistTimer( this->timers_.mtxRedistTime_ );
#endif

    if ( is_contiguous_ == true ) {
      Util::get_ccs_helper<
        MatrixAdapter<Matrix>,slu_type,local_ordinal_type,local_ordinal_type>
        ::do_get(this->matrixA_.ptr(), nzvals_(), rowind_(), colptr_(),
            nnz_ret, ROOTED, ARBITRARY, this->rowIndexBase_);
    }
    else {
      Util::get_ccs_helper<
        MatrixAdapter<Matrix>,slu_type,local_ordinal_type,local_ordinal_type>
        ::do_get(this->matrixA_.ptr(), nzvals_(), rowind_(), colptr_(),
            nnz_ret, CONTIGUOUS_AND_ROOTED, ARBITRARY, this->rowIndexBase_);
    }
  }


  if( this->root_ ){
    TEUCHOS_TEST_FOR_EXCEPTION( nnz_ret != as<local_ordinal_type>(this->globalNumNonZeros_),
                        std::runtime_error,
                        "Did not get the expected number of non-zero vals");
  }

  return true;
}


template<class Matrix, class Vector>
const char* KLU2<Matrix,Vector>::name = "KLU2";


} // end namespace Amesos2

#endif  // AMESOS2_KLU2_DEF_HPP
