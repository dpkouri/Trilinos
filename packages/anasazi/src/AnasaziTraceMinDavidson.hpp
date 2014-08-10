// @HEADER
// ***********************************************************************
//
//                 Anasazi: Block Eigensolvers Package
//                 Copyright (2004) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ***********************************************************************
// @HEADER

/*! \file AnasaziTraceMinDavidson.hpp
  \brief Implementation of the TraceMin-Davidson method
*/

#ifndef ANASAZI_TRACEMIN_DAVIDSON_HPP
#define ANASAZI_TRACEMIN_DAVIDSON_HPP

#include "AnasaziConfigDefs.hpp"
#include "AnasaziEigensolver.hpp"
#include "AnasaziMultiVecTraits.hpp"
#include "AnasaziMatOrthoManager.hpp"
#include "AnasaziOperatorTraits.hpp"
#include "AnasaziTraceMinBase.hpp"

#include "Teuchos_ScalarTraits.hpp"
#include "Teuchos_SerialDenseMatrix.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_TimeMonitor.hpp"


namespace Anasazi {
namespace Experimental {

  /*! \class Anasazi::TraceMinDavidson
  
      \brief This class implements a TraceMin-Davidson iteration for solving
      symmetric generalized eigenvalue problems

      This method is described in <em>The trace minimization method for the
      symmetric generalized eigenvalue problem</em>, A. Sameh and Z. Tong, 
      Journal of Computational and Applied Mathematics, 123, pp 155-175 (2000)
        
      \ingroup anasazi_solver_framework

      \author Alicia Klinvex
  */

  template <class ScalarType, class MV, class OP>
  class TraceMinDavidson : public TraceMinBase<ScalarType,MV,OP> { 
  public:

    /*! \brief %TraceMinBase constructor with eigenproblem, solver utilities, and parameter list of solver options.
     *
     * This constructor takes pointers required by the eigensolver, in addition
     * to a parameter list of options for the eigensolver. These options include the following (in addition to those
     * of TraceMinBase):
     *   - "Block Size" - an \c int specifying the block size used by the algorithm. This can also be specified using the setBlockSize() method.
     *   - "Num Blocks" - an \c int specifying the maximum number of blocks allocated for the solver basis.
     */
    TraceMinDavidson( const Teuchos::RCP<Eigenproblem<ScalarType,MV,OP> >    &problem, 
                      const Teuchos::RCP<SortManager<typename Teuchos::ScalarTraits<ScalarType>::magnitudeType> > &sorter,
                      const Teuchos::RCP<OutputManager<ScalarType> >         &printer,
                      const Teuchos::RCP<StatusTest<ScalarType,MV,OP> >      &tester,
                      const Teuchos::RCP<MatOrthoManager<ScalarType,MV,OP> > &ortho,
                      Teuchos::ParameterList &params 
                    );

  private:
    //
    // Convenience typedefs
    //
    typedef MultiVecTraits<ScalarType,MV> MVT;
    typedef OperatorTraits<ScalarType,MV,OP> OPT;
    typedef Teuchos::ScalarTraits<ScalarType> SCT;
    typedef typename SCT::magnitudeType MagnitudeType;

    // TraceMin specific methods
    void addToBasis(const Teuchos::RCP<const MV> Delta);
  };

  //////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////
  //
  // Implementations
  //
  //////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////



  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Constructor
  template <class ScalarType, class MV, class OP>
  TraceMinDavidson<ScalarType,MV,OP>::TraceMinDavidson(
        const Teuchos::RCP<Eigenproblem<ScalarType,MV,OP> >    &problem, 
        const Teuchos::RCP<SortManager<typename Teuchos::ScalarTraits<ScalarType>::magnitudeType> > &sorter,
        const Teuchos::RCP<OutputManager<ScalarType> >         &printer,
        const Teuchos::RCP<StatusTest<ScalarType,MV,OP> >      &tester,
        const Teuchos::RCP<MatOrthoManager<ScalarType,MV,OP> > &ortho,
        Teuchos::ParameterList &params
        ) :
    TraceMinBase<ScalarType,MV,OP>(problem,sorter,printer,tester,ortho,params)
  {     
  }


  //////////////////////////////////////////////////////////////////////////////////////////////////
  // 1. Project Delta so that V' M Delta = 0 and Q' M Delta = 0
  // 2. Normalize Delta so that Delta' M Delta = I
  // 3. Add Delta to the end of V: V = [V Delta]
  // 4. Update KV and MV
  template <class ScalarType, class MV, class OP>
  void TraceMinDavidson<ScalarType,MV,OP>::addToBasis(Teuchos::RCP<const MV> Delta)
  {
    // TODO: We should also test the row length and map, etc...
    TEUCHOS_TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*Delta) != this->blockSize_, std::invalid_argument,
           "Anasazi::TraceMinDavidson::addToBasis(): Delta does not have blockSize_ columns");

    int rank;
    // Vector of indices
    std::vector<int> curind(this->curDim_), newind(this->blockSize_);
    // Pointer to the meaningful parts of V, KV, and MV
    Teuchos::RCP<MV> lclV, lclKV, lclMV;
    // Holds the vectors we project against
    Teuchos::Array< Teuchos::RCP<const MV> > projVecs = this->auxVecs_;

    // Get the existing parts of the basis and add them to the list of things we project against
    for(int i=0; i<this->curDim_; i++)
      curind[i] = i;
    lclV = MVT::CloneViewNonConst(*this->V_,curind);
    projVecs.push_back(lclV);

    // Get the new part of the basis (where we're going to insert Delta)
    for (int i=0; i<this->blockSize_; ++i) 
      newind[i] = this->curDim_ + i;
    lclV = MVT::CloneViewNonConst(*this->V_,newind);

    // Insert Delta at the end of V
    MVT::SetBlock(*Delta,newind,*this->V_);
    this->curDim_ += this->blockSize_;

    // Project out the components of Delta in the direction of V
    if(this->hasM_)
    {
      // It is more efficient to provide the orthomanager with MV
      Teuchos::Array< Teuchos::RCP<const MV> > MprojVecs = this->MauxVecs_;
      lclMV = MVT::CloneViewNonConst(*this->MV_,curind);
      MprojVecs.push_back(lclMV);

      // Compute M*Delta
      lclMV = MVT::CloneViewNonConst(*this->MV_,newind);
      {
        #ifdef ANASAZI_TEUCHOS_TIME_MONITOR
          Teuchos::TimeMonitor lcltimer( *this->timerMOp_ );
        #endif
        this->count_ApplyM_+= this->blockSize_;
        OPT::Apply(*this->MOp_,*lclV,*lclMV);
      }

      {
        #ifdef ANASAZI_TEUCHOS_TIME_MONITOR
          Teuchos::TimeMonitor lcltimer( *this->timerOrtho_ );
        #endif

        // Project and normalize Delta
        rank = this->orthman_->projectAndNormalizeMat(*lclV,projVecs,
               Teuchos::tuple(Teuchos::RCP< Teuchos::SerialDenseMatrix< int, ScalarType > >(Teuchos::null)),
               Teuchos::null,lclMV,MprojVecs);
      }

      MprojVecs.pop_back();
    }
    else
    {
      #ifdef ANASAZI_TEUCHOS_TIME_MONITOR
        Teuchos::TimeMonitor lcltimer( *this->timerOrtho_ );
      #endif

      // Project and normalize Delta
      rank = this->orthman_->projectAndNormalizeMat(*lclV,projVecs);
    }

    projVecs.pop_back();

    TEUCHOS_TEST_FOR_EXCEPTION(rank != this->blockSize_,TraceMinBaseOrthoFailure,
           "Anasazi::TraceMinDavidson::addToBasis(): Couldn't generate basis of full rank.");

    // Update KV
    if(this->Op_ != Teuchos::null)
    {
      #ifdef ANASAZI_TEUCHOS_TIME_MONITOR
        Teuchos::TimeMonitor lcltimer( *this->timerOp_ );
      #endif
      this->count_ApplyOp_+= this->blockSize_;

      lclKV = MVT::CloneViewNonConst(*this->KV_,newind);
      OPT::Apply(*this->Op_,*lclV,*lclKV);
    }
  }

}} // End of namespace Anasazi

#endif

// End of file AnasaziTraceMinDavidson.hpp