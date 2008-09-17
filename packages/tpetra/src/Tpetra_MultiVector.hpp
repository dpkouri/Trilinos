// @HEADER
// ***********************************************************************
// 
//          Tpetra: Templated Linear Algebra Services Package
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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ***********************************************************************
// @HEADER

// TODO: some of these arrayview objects should be something else, like Ptr
// TODO: consider requiring that ArrayView objects are the exact size needed, and no larger.
// TODO: with contiguous MVs, some of the level one blas routines below can be turned from multiple calls to one call for best efficiency (eliminate loop over numVecs)

#ifndef TPETRA_MULTIVECTOR_HPP
#define TPETRA_MULTIVECTOR_HPP

#include <Teuchos_TestForException.hpp>
#include <Teuchos_as.hpp>
#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_OrdinalTraits.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_Ptr.hpp>

#include "Tpetra_MultiVectorDecl.hpp"
#include "Tpetra_MultiVectorData.hpp"

namespace Tpetra {

  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::MultiVector(const Map<Ordinal> &map, Ordinal NumVectors, bool zeroOut) 
    : DistObject<Ordinal,Scalar>(map, map.getPlatform()->createComm(), "Tpetra::MultiVector")
  {
    using Teuchos::as;
    TEST_FOR_EXCEPTION(NumVectors < 1, std::invalid_argument,
        "Tpetra::MultiVector::MultiVector(): NumVectors must be strictly positive.");
    const Ordinal myLen = myLength();
    MVData_ = Teuchos::rcp( new MultiVectorData<Ordinal,Scalar>() );
    MVData_->constantStride_ = true;
    MVData_->stride_ = myLen;
    MVData_->values_ = Teuchos::arcp<Scalar>(NumVectors*myLen);
    if (zeroOut) {
      std::fill(MVData_->values_.begin(),MVData_->values_.end(),Teuchos::ScalarTraits<Scalar>::zero());
    }
    MVData_->pointers_ = Teuchos::arcp<Teuchos::ArrayRCP<Scalar> >(NumVectors);
    for (Ordinal i = as<Ordinal>(0); i < NumVectors; ++i) {
      MVData_->pointers_[i] = MVData_->values_.persistingView(i*myLen,myLen);
    }
  }


  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::MultiVector(const MultiVector<Ordinal,Scalar> &source) 
    : DistObject<Ordinal,Scalar>(source)
  {
    // copy data from the source MultiVector into this multivector
    // this multivector will be allocated with constant stride, even if the source multivector does not have constant stride
    using Teuchos::as;
    const Ordinal myLen   = myLength();
    const Ordinal numVecs = source.numVectors();
    MVData_ = Teuchos::rcp( new MultiVectorData<Ordinal,Scalar>() );
    MVData_->constantStride_ = true;
    MVData_->stride_ = myLen;
    MVData_->values_ = Teuchos::arcp<Scalar>(numVecs*myLen);
    MVData_->pointers_ = Teuchos::arcp<Teuchos::ArrayRCP<Scalar> >(numVecs);
    for (Ordinal i = as<Ordinal>(0); i < numVecs; ++i) {
      MVData_->pointers_[i] = MVData_->values_.persistingView(i*myLen,myLen);
      std::copy( source.MVData_->pointers_[i].begin(), source.MVData_->pointers_[i].end(), MVData_->pointers_[i].begin() );
    }
  }


  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::MultiVector(const Map<Ordinal> &map, const Teuchos::ArrayView<const Scalar> &A, Ordinal LDA, Ordinal NumVectors)
    : DistObject<Ordinal,Scalar>(map, map.getPlatform()->createComm(), "Tpetra::MultiVector")
  {
    using Teuchos::ArrayView;
    using std::copy;
    using Teuchos::as;
    TEST_FOR_EXCEPTION(NumVectors < 1, std::invalid_argument,
        "Tpetra::MultiVector::MultiVector(): NumVectors must be strictly positive.");
    const Ordinal myLen = myLength();
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(LDA < myLen, std::invalid_argument,
        "Tpetra::MultiVector::MultiVector(): LDA must be large enough to accomodate the local entries.");
    // need LDA*(NumVectors-1)+myLen elements in A
    TEST_FOR_EXCEPTION(A.size() < LDA*(NumVectors-1)+myLen, std::runtime_error,
        "Tpetra::MultiVector::MultiVector(): A,LDA must be large enough to accomodate the local entries.");
#endif
    MVData_ = Teuchos::rcp( new MultiVectorData<Ordinal,Scalar>() );
    MVData_->constantStride_ = true;
    MVData_->stride_ = myLen;
    MVData_->values_ = Teuchos::arcp<Scalar>(NumVectors*myLen);
    MVData_->pointers_ = Teuchos::arcp<Teuchos::ArrayRCP<Scalar> >(NumVectors);
    for (Ordinal i = as<Ordinal>(0); i < NumVectors; ++i) {
      MVData_->pointers_[i] = MVData_->values_.persistingView(i*myLen,myLen);
      // copy data from A to my internal data structure
      ArrayView<const Scalar> Aptr = A(i*LDA,myLen);
      copy(Aptr.begin(),Aptr.end(),MVData_->pointers_[i].begin());
    }
  }


  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::MultiVector(const Map<Ordinal> &map, Teuchos::RCP<MultiVectorData<Ordinal,Scalar> > &mvdata) 
    : DistObject<Ordinal,Scalar>(map, map.getPlatform()->createComm(), "Tpetra::MultiVector"), MVData_(mvdata)
  {}


  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::MultiVector(const Map<Ordinal> &map, const Teuchos::ArrayView<const Teuchos::ArrayView<const Scalar> > &arrayOfArrays, Ordinal NumVectors)
    : DistObject<Ordinal,Scalar>(map, map.getPlatform()->createComm(), "Tpetra::MultiVector")
  {
    TEST_FOR_EXCEPT(true);
  }


  template <typename Ordinal, typename Scalar> 
  MultiVector<Ordinal,Scalar>::~MultiVector()
  {}


  template <typename Ordinal, typename Scalar> 
  bool MultiVector<Ordinal,Scalar>::constantStride() const
  {
    return MVData_->constantStride_;
  }


  template <typename Ordinal, typename Scalar> 
  Ordinal MultiVector<Ordinal,Scalar>::myLength() const
  {
    return this->getMap().getNumMyEntries();
  }


  template <typename Ordinal, typename Scalar> 
  Ordinal MultiVector<Ordinal,Scalar>::globalLength() const
  {
    return this->getMap().getNumGlobalEntries();
  }


  template <typename Ordinal, typename Scalar> 
  Ordinal MultiVector<Ordinal,Scalar>::stride() const
  {
    return MVData_->stride_;
  }


  template <typename Ordinal, typename Scalar> 
  void MultiVector<Ordinal,Scalar>::print(std::ostream &os) const
  {
    using std::endl;
    Teuchos::RCP<const Teuchos::Comm<Ordinal> > comm = this->getMap().getComm();
    const int myImageID = comm->getRank();
    const int numImages = comm->getSize();
    for(int imageCtr = 0; imageCtr < numImages; ++imageCtr) {
      if (myImageID == imageCtr) {
        if (myImageID == 0) {
          os << "Number of vectors: " << numVectors() << endl;
          os << "Global length: " << globalLength() << endl;
        }
        os << "Local length: " << myLength() << endl;
        os << "Local stride: " << stride() << endl;
        os << "Constant stride: " << (constantStride() ? "true" : "false") << endl;
      }
      // Do a few global ops to give I/O a chance to complete
      comm->barrier();
      comm->barrier();
      comm->barrier();
    }
  }


  template <typename Ordinal, typename Scalar> 
  void MultiVector<Ordinal,Scalar>::printValues(std::ostream &os) const
  {
    TEST_FOR_EXCEPT(true);
  }


  template<typename Ordinal, typename Scalar>
  bool MultiVector<Ordinal,Scalar>::checkSizes(const DistObject<Ordinal,Scalar> &sourceObj) 
  {
    const MultiVector<Ordinal,Scalar> &A = dynamic_cast<const MultiVector<Ordinal,Scalar>&>(sourceObj);
    return A.numVectors() == this->numVectors();
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::copyAndPermute(
      const DistObject<Ordinal,Scalar> & sourceObj,
      Ordinal numSameIDs,
      Ordinal numPermuteIDs,
      const Teuchos::ArrayView<const Ordinal> &permuteToLIDs,
      const Teuchos::ArrayView<const Ordinal> &permuteFromLIDs)
  {
    (void)sourceObj;
    (void)numSameIDs;
    (void)numPermuteIDs;
    (void)permuteToLIDs;
    (void)permuteFromLIDs;
    TEST_FOR_EXCEPT(true);
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::packAndPrepare(
      const DistObject<Ordinal,Scalar> & sourceObj,
      Ordinal numExportIDs,
      const Teuchos::ArrayView<const Ordinal> &exportLIDs,
      const Teuchos::ArrayView<Scalar> &exports,
      Ordinal &packetSize,
      Distributor<Ordinal> &distor)
  {
    (void)sourceObj;
    (void)numExportIDs;
    (void)exportLIDs;
    (void)exports;
    (void)packetSize;
    (void)distor;
    TEST_FOR_EXCEPT(true);
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::unpackAndCombine(
      Ordinal numImportIDs,
      const Teuchos::ArrayView<const Ordinal> &importLIDs,
      const Teuchos::ArrayView<const Scalar> &imports,
      Distributor<Ordinal> &distor,
      CombineMode CM)
  {
    (void)numImportIDs;
    (void)importLIDs;
    (void)imports;
    (void)distor;
    (void)CM;
    TEST_FOR_EXCEPT(true);
  }


  template<typename Ordinal, typename Scalar>
  Ordinal MultiVector<Ordinal,Scalar>::numVectors() const 
  {
    return Teuchos::as<Ordinal>(MVData_->pointers_.size());
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::dot(
      const MultiVector<Ordinal,Scalar> &A, 
      const Teuchos::ArrayView<Scalar> &dots) const 
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    const Ordinal ONE = Teuchos::OrdinalTraits<Ordinal>::one();
    // compute local dot products of *this and A
    // sum these across all nodes
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()), std::runtime_error,
        "Tpetra::MultiVector::dots(): MultiVectors must have compatible Maps.");
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::dots(): MultiVectors must have the same number of vectors.");
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(dots.size() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::dots(A,dots): dots.size() must be as large as the number of vectors in *this and A.");
#endif
    Teuchos::Array<Scalar> ldots(numVecs);
    for (Ordinal i=ZERO; i<numVecs; ++i) {
      ldots[i] = blas.DOT(MVData_->pointers_[i].size(),MVData_->pointers_[i].getRawPtr(),ONE,A[i].getRawPtr(),ONE);
    }
    if (this->getMap().isDistributed()) {
      // only combine if we are a distributed MV
      Teuchos::reduceAll(*this->getMap().getComm(),Teuchos::REDUCE_SUM,numVecs,ldots.getRawPtr(),dots.getRawPtr());
    }
    else {
      std::copy(ldots.begin(),ldots.end(),dots.begin());
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::norm1(
      const Teuchos::ArrayView<typename Teuchos::ScalarTraits<Scalar>::magnitudeType> &norms) const
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    typedef typename Teuchos::ScalarTraits<Scalar>::magnitudeType Mag;
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    const Ordinal ONE = Teuchos::OrdinalTraits<Ordinal>::one();
    // compute local components of the norms
    // sum these across all nodes
    const Ordinal numVecs = this->numVectors();
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(norms.size() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::norm1(norms): norms.size() must be as large as the number of vectors in *this.");
#endif
    Teuchos::Array<Mag> lnorms(numVecs);
    for (Ordinal i=ZERO; i<numVecs; ++i) {
      lnorms[i] = blas.ASUM(MVData_->pointers_[i].size(),MVData_->pointers_[i].getRawPtr(),ONE);
    }
    if (this->getMap().isDistributed()) {
      // only combine if we are a distributed MV
      Teuchos::reduceAll(*this->getMap().getComm(),Teuchos::REDUCE_SUM,numVecs,lnorms.getRawPtr(),norms.getRawPtr());
    }
    else {
      std::copy(lnorms.begin(),lnorms.end(),norms.begin());
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::norm2(
      const Teuchos::ArrayView<typename Teuchos::ScalarTraits<Scalar>::magnitudeType> &norms) const
  {
    using Teuchos::ScalarTraits;
    using Teuchos::ArrayView;
    typedef typename ScalarTraits<Scalar>::magnitudeType Mag;
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    // compute local components of the norms
    // sum these across all nodes
    const Ordinal numVecs = this->numVectors();
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(norms.size() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::norm2(norms): norms.size() must be as large as the number of vectors in *this.");
#endif
    Teuchos::Array<Mag> lnorms(numVecs,ScalarTraits<Mag>::zero());
    for (Ordinal j=ZERO; j<numVecs; ++j) {
      Teuchos::ArrayRCP<const Scalar> cpos = MVData_->pointers_[j].getConst();
      for (; cpos != cpos.end(); ++cpos) {
        lnorms[j] += ScalarTraits<Scalar>::magnitude( 
                       (*cpos) * ScalarTraits<Scalar>::conjugate(*cpos)
                     );
      }
    }
    if (this->getMap().isDistributed()) {
      // only combine if we are a distributed MV
      Teuchos::reduceAll(*this->getMap().getComm(),Teuchos::REDUCE_SUM,numVecs,lnorms.getRawPtr(),norms.getRawPtr());
    }
    else {
      std::copy(lnorms.begin(),lnorms.end(),norms.begin());
    }
    for (typename ArrayView<Mag>::iterator n = norms.begin(); n != norms.begin()+numVecs; ++n) {
      *n = ScalarTraits<Mag>::squareroot(*n);
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::normWeighted(
      const MultiVector<Ordinal,Scalar> &weights,
      const Teuchos::ArrayView<typename Teuchos::ScalarTraits<Scalar>::magnitudeType> &norms) const
  {
    using Teuchos::ScalarTraits;
    using Teuchos::ArrayView;
    using Teuchos::ArrayRCP;
    using Teuchos::Array;
    using Teuchos::as;
    typedef ScalarTraits<Scalar> SCT;
    typedef typename SCT::magnitudeType Mag;
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    const Ordinal ONE  = Teuchos::OrdinalTraits<Ordinal>::one();
    const Ordinal numImages = this->getMap().getComm()->getSize();
    bool OneW = false;
    const Ordinal numVecs = this->numVectors();
    if (weights.numVectors() == ONE) {
      OneW = true;
    }
    else {
      TEST_FOR_EXCEPTION(weights.numVectors() != numVecs, std::runtime_error,
          "Tpetra::MultiVector::normWeighted(): MultiVector of weights must contain either one vector or the same number of vectors as this.");
    }
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(weights.getMap()), std::runtime_error,
        "Tpetra::MultiVector::normWeighted(): MultiVectors must have compatible Maps.");
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(norms.size() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::normWeighted(): norms.size() must be as large as the number of vectors in *this.");
#endif
    // compute local components of the norms
    // sum these across all nodes
    Array<Mag> lnorms(numVecs,ScalarTraits<Mag>::zero());
    for (Ordinal j=ZERO; j<numVecs; ++j) {
      ArrayRCP<const Scalar> wpos = (OneW ? weights[0] : weights[j]);
      ArrayRCP<const Scalar> cpos = MVData_->pointers_[j].getConst();
      for (; cpos != cpos.end(); ++cpos, ++wpos) {
        Scalar tmp = *cpos / *wpos;
        lnorms[j] += SCT::magnitude( tmp * SCT::conjugate(tmp) );
      }
    }
    if (this->getMap().isDistributed()) {
      // only combine if we are a distributed MV
      Teuchos::reduceAll(*this->getMap().getComm(),Teuchos::REDUCE_SUM,numVecs,lnorms.getRawPtr(),norms.getRawPtr());
    }
    else {
      std::copy(lnorms.begin(),lnorms.end(),norms.begin());
    }
    for (typename ArrayView<Mag>::iterator n = norms.begin(); n != norms.begin()+numVecs; ++n) {
      *n = ScalarTraits<Mag>::squareroot(*n/as<Mag>(numImages));
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::normInf(
      const Teuchos::ArrayView<typename Teuchos::ScalarTraits<Scalar>::magnitudeType> &norms) const
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    typedef typename Teuchos::ScalarTraits<Scalar>::magnitudeType Mag;
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    const Ordinal ONE = Teuchos::OrdinalTraits<Ordinal>::one();
    // compute local components of the norms
    // sum these across all nodes
    const Ordinal numVecs = this->numVectors();
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(norms.size() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::normInf(norms): norms.size() must be as large as the number of vectors in *this.");
#endif
    Teuchos::Array<Mag> lnorms(numVecs);
    for (Ordinal i=ZERO; i<numVecs; ++i) {
      // careful!!! IAMAX returns FORTRAN-style (i.e., one-based) index. subtract ind by one
      Ordinal ind = blas.IAMAX(MVData_->pointers_[i].size(),MVData_->pointers_[i].getRawPtr(),ONE) - ONE;
      lnorms[i] = Teuchos::ScalarTraits<Scalar>::magnitude( MVData_->pointers_[i][ind] );
    }
    if (this->getMap().isDistributed()) {
      // only combine if we are a distributed MV
      Teuchos::reduceAll(*this->getMap().getComm(),Teuchos::REDUCE_MAX,numVecs,lnorms.getRawPtr(),norms.getRawPtr());
    }
    else {
      std::copy(lnorms.begin(),lnorms.end(),norms.begin());
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::random() 
  {
    const Ordinal ZERO = Teuchos::OrdinalTraits<Ordinal>::zero();
    const Ordinal myLen   = this->myLength();
    const Ordinal numVecs = this->numVectors();
    for (Ordinal j=ZERO; j<numVecs; ++j) {
      Teuchos::ArrayRCP<Scalar> cpos = MVData_->pointers_[j];
      for (Ordinal i=ZERO; i<myLen; ++i) {
        *cpos = Teuchos::ScalarTraits<Scalar>::random();
        ++cpos;
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::putScalar(const Scalar &alpha) 
  {
    using Teuchos::OrdinalTraits;
    using Teuchos::ArrayRCP;
    const Ordinal numVecs = this->numVectors();
    for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
      ArrayRCP<Scalar> &curpos = MVData_->pointers_[i];
      std::fill(curpos.begin(),curpos.end(),alpha);
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::scale(const Scalar &alpha) 
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    using Teuchos::OrdinalTraits;
    const Ordinal ONE = Teuchos::OrdinalTraits<Ordinal>::one();
    using Teuchos::ArrayRCP;
    const Ordinal numVecs = this->numVectors();
    if (alpha == Teuchos::ScalarTraits<Scalar>::one()) {
      // do nothing
    }
    else if (alpha == Teuchos::ScalarTraits<Scalar>::zero()) {
      putScalar(alpha);
    }
    else {
      for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
        ArrayRCP<Scalar> &curpos = MVData_->pointers_[i];
        blas.SCAL(curpos.size(),alpha,curpos.getRawPtr(),ONE);
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::scale(const Scalar &alpha, const MultiVector<Ordinal,Scalar> &A) 
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    using Teuchos::OrdinalTraits;
    using Teuchos::ArrayRCP;
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()), std::runtime_error,
        "Tpetra::MultiVector::scale(): MultiVectors must have compatible Maps.");
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::scale(): MultiVectors must have the same number of vectors.");
    const Ordinal ONE = Teuchos::OrdinalTraits<Ordinal>::one();
    if (alpha == Teuchos::ScalarTraits<Scalar>::zero()) {
      putScalar(alpha); // set me = 0.0
    }
    else if (alpha == Teuchos::ScalarTraits<Scalar>::one()) {
      *this = A;        // set me = A
    }
    else {
      // set me == alpha*A
      for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
        ArrayRCP<Scalar> &curpos = MVData_->pointers_[i];
        ArrayRCP<Scalar> &Apos = A.MVData_->pointers_[i];
        // copy A to *this
        blas.COPY(curpos.size(),Apos.getRawPtr(),ONE,curpos.getRawPtr(),ONE);
        // then scale *this in-situ
        blas.SCAL(curpos.size(),alpha,curpos.getRawPtr(),ONE);
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::reciprocal(const MultiVector<Ordinal,Scalar> &A) 
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    using Teuchos::OrdinalTraits;
    using Teuchos::ScalarTraits;
    using Teuchos::ArrayRCP;
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()), std::runtime_error,
        "Tpetra::MultiVector::reciprocal(): MultiVectors must have compatible Maps.");
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::reciprocal(): MultiVectors must have the same number of vectors.");
    for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
      ArrayRCP<Scalar> &curpos = MVData_->pointers_[i];
      ArrayRCP<Scalar> &Apos = A.MVData_->pointers_[i];
      for (; curpos != curpos.end(); ++curpos, ++Apos) {
#ifdef TEUCHOS_DEBUG
        TEST_FOR_EXCEPTION( ScalarTraits<Scalar>::magnitude(*Apos) <= ScalarTraits<Scalar>::sfmin() ||
            *Apos == ScalarTraits<Scalar>::sfmin(), std::runtime_error,
            "Tpetra::MultiVector::reciprocal(): element of A was zero or too small to invert: " << *Apos );
#endif
        *curpos = ScalarTraits<Scalar>::one()/(*Apos);
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::abs(const MultiVector<Ordinal,Scalar> &A) 
  {
    Teuchos::BLAS<Ordinal,Scalar> blas;
    using Teuchos::OrdinalTraits;
    using Teuchos::ArrayRCP;
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::abs(): MultiVectors must have the same number of vectors.");
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()), std::runtime_error,
        "Tpetra::MultiVector::abs(): MultiVectors must have compatible Maps.");
    for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
      ArrayRCP<Scalar> &curpos = MVData_->pointers_[i];
      ArrayRCP<Scalar> &Apos = A.MVData_->pointers_[i];
      for (; curpos != curpos.end(); ++curpos, ++Apos) {
        *curpos = Teuchos::ScalarTraits<Scalar>::magnitude(*Apos);
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::update(const Scalar &alpha, const MultiVector<Ordinal,Scalar> &A, const Scalar &beta) 
  {
    typedef Teuchos::ScalarTraits<Scalar> ST;
    using Teuchos::OrdinalTraits;
    using Teuchos::ArrayRCP;
    if (alpha == ST::zero()) {
      scale(beta);
    }
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()), std::runtime_error,
        "Tpetra::MultiVector::update(): MultiVectors must have compatible Maps.");
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::update(): MultiVectors must have the same number of vectors.");

    if (beta == ST::zero()) { // this = alpha*A
      scale(alpha,A);
      return;
    }
    else if (beta == ST::one()) { // this = this + alpha*A
      if (alpha == ST::one()) { // this = this + A
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = A.MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos) { *curpos = (*curpos) + (*Apos); }
        }
      }
      else { // this = this + alpha*A
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = A.MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos) { *curpos = (*curpos) + alpha*(*Apos); }
        }
      }
    }
    else { // this = beta*this + alpha*A
      if (alpha == ST::one()) { // this = beta*this + A
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = A.MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos) { *curpos = beta*(*curpos) + (*Apos); }
        }
      }
      else { // this = beta*this + alpha*A
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = A.MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos) { *curpos = beta*(*curpos) + alpha*(*Apos); }
        }
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::update(const Scalar &alpha, const MultiVector<Ordinal,Scalar> &A, const Scalar &beta, const MultiVector<Ordinal,Scalar> &B, const Scalar &gamma)
  {
    typedef Teuchos::ScalarTraits<Scalar> ST;
    using Teuchos::OrdinalTraits;
    using Teuchos::ArrayRCP;
    if (alpha == ST::zero()) {
      update(beta,B,gamma);
    }
    else if (beta == ST::zero()) {
      update(alpha,A,gamma);
    }
    const Ordinal numVecs = this->numVectors();
    TEST_FOR_EXCEPTION( !this->getMap().isCompatible(A.getMap()) || !this->getMap().isCompatible(B.getMap()),
        std::runtime_error,
        "Tpetra::MultiVector::update(): MultiVectors must have compatible Maps.");
    TEST_FOR_EXCEPTION(A.numVectors() != numVecs || B.numVectors() != numVecs, std::runtime_error,
        "Tpetra::MultiVector::update(): MultiVectors must have the same number of vectors.");
    // determine if alpha==1 xor beta==1
    // if only one of these is 1.0, make it alpha
    Teuchos::Ptr<const MultiVector<Ordinal,Scalar> > Aptr = Teuchos::ptr(&A), Bptr = Teuchos::ptr(&B);
    Teuchos::Ptr<const Scalar> lalpha = Teuchos::ptr(&alpha),
                               lbeta  = Teuchos::ptr(&beta);
    if (alpha!=ST::one() && beta==ST::one()) {
      // switch them
      Aptr = Teuchos::ptr(&B);
      Bptr = Teuchos::ptr(&A);
      lalpha = Teuchos::ptr(&beta);
      lbeta  = Teuchos::ptr(&alpha);
    }

    if (gamma == ST::zero()) { // this = lalpha*A + lbeta*B
      if (*lalpha == ST::one()) {
        if (*lbeta == ST::one()) { // this = gamma*this + A + B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*Apos) + (*Bpos); }
          }
        }
        else { // this = A + lbeta*B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*Apos) + (*lbeta)*(*Bpos); }
          }
        }
      }
      else { // this = lalpha*A + lbeta*B
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*lalpha)*(*Apos) + (*lbeta)*(*Bpos); }
        }
      }
    }
    else if (gamma == ST::one()) { // this = this + lalpha*A + lbeta*B
      if ((*lalpha) == ST::one()) {
        if ((*lbeta) == ST::one()) { // this = this + A + B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*curpos) + (*Apos) + (*Bpos); }
          }
        }
        else { // this = this + A + lbeta*B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*curpos) + (*Apos) + (*lbeta)*(*Bpos); }
          }
        }
      }
      else { // this = this + lalpha*A + lbeta*B
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = (*curpos) + (*lalpha)*(*Apos) + (*lbeta)*(*Bpos); }
        }
      }
    }
    else { // this = gamma*this + lalpha*A + lbeta*B
      if ((*lalpha) == ST::one()) {
        if ((*lbeta) == ST::one()) { // this = gamma*this + A + B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = gamma*(*curpos) + (*Apos) + (*Bpos); }
          }
        }
        else { // this = gamma*this + A + lbeta*B
          for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
            ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
            for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = gamma*(*curpos) + (*Apos) + (*lbeta)*(*Bpos); }
          }
        }
      }
      else { // this = gamma*this + lalpha*A + lbeta*B
        for (Ordinal i = OrdinalTraits<Ordinal>::zero(); i < numVecs; ++i) {
          ArrayRCP<Scalar> curpos = MVData_->pointers_[i]; ArrayRCP<const Scalar> Apos = Aptr->MVData_->pointers_[i].getConst(), Bpos = Bptr->MVData_->pointers_[i].getConst();
          for (; curpos != curpos.end(); ++curpos, ++Apos, ++Bpos) { *curpos = gamma*(*curpos) + (*lalpha)*(*Apos) + (*lbeta)*(*Bpos); }
        }
      }
    }
  }


  template<typename Ordinal, typename Scalar>
  Teuchos::ArrayRCP<const Scalar> MultiVector<Ordinal,Scalar>::operator[](Ordinal i) const
  {
    // teuchos does the bounds checking here, if TEUCHOS_DEBUG
    return MVData_->pointers_[i].getConst();
  }

  template<typename Ordinal, typename Scalar>
  MultiVector<Ordinal,Scalar>& MultiVector<Ordinal,Scalar>::operator=(const MultiVector<Ordinal,Scalar> &source) {
    // Check for special case of this=Source
    if (this != &source) {
      TEST_FOR_EXCEPTION( !this->getMap().isCompatible(source.getMap()), std::runtime_error,
          "Tpetra::MultiVector::operator=(): MultiVectors must have compatible Maps.");
      if (constantStride() && source.constantStride() && myLength()==stride() && source.myLength()==source.stride()) {
        // can copy in one call
        std::copy( source.MVData_->values_.begin(), source.MVData_->values_.begin() + source.numVectors()*source.stride(),
                   MVData_->values_.begin() );
      }
      else {
        for (Ordinal j=0; j<numVectors(); ++j) {
          std::copy( source.MVData_->pointers_[j].begin(), source.MVData_->pointers_[j].end(), 
                     MVData_->pointers_[j] );
        }
      }
    }
    return(*this);
  }

  /*
  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subCopy(const Teuchos::Range1D &colRng) const
  {
    // FINISH
    TEST_FOR_EXCEPT(true);
    return Teuchos::null;
  }
  */

  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subCopy(const Teuchos::ArrayView<const Teuchos_Index> &cols) const
  {
    // TODO: in debug mode, do testing that cols[j] are distinct
    Ordinal numCols = cols.size();
    // allocate new MV
    const bool zeroData = false;
    Teuchos::RCP<MultiVector<Ordinal,Scalar> > mv = rcp( new MultiVector<Ordinal,Scalar>(this->getMap(),numCols,zeroData) );
    // copy data from *this into mv
    for (Ordinal j=0; j<numCols; ++j)
    {
      std::copy( MVData_->pointers_[cols[j]].begin(), MVData_->pointers_[cols[j]].end(), mv->MVData_->pointers_[j].begin() );
    }
    return mv;
  }

  /*
  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subView(const Teuchos::Range1D &colRng) 
  {
    // FINISH
    TEST_FOR_EXCEPT(true);
    return Teuchos::null;
  }
  */

  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subView(const Teuchos::ArrayView<const Teuchos_Index> &cols) 
  {
    using Teuchos::as;
    const Ordinal numVecs = cols.size();
    Teuchos::RCP<MultiVectorData<Ordinal,Scalar> > mvdata = Teuchos::rcp( new MultiVectorData<Ordinal,Scalar>() );
    mvdata->constantStride_ = false;
    mvdata->stride_ = stride();
    mvdata->values_ = MVData_->values_;
    mvdata->pointers_ = Teuchos::arcp<Teuchos::ArrayRCP<Scalar> >(numVecs);
    for (Ordinal j = as<Ordinal>(0); j < numVecs; ++j) {
      mvdata->pointers_[j] = MVData_->pointers_[cols[j]];
    }
    Teuchos::RCP<MultiVector<Ordinal,Scalar> > mv = Teuchos::rcp( new MultiVector<Ordinal,Scalar>(this->getMap(),mvdata) );
    return mv;
  }

  /*
  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<const MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subViewConst(const Teuchos::Range1D &colRng) const 
  {
    // FINISH
    TEST_FOR_EXCEPT(true);
    return Teuchos::null;
  }
  */

  template<typename Ordinal, typename Scalar>
  Teuchos::RCP<const MultiVector<Ordinal,Scalar> > MultiVector<Ordinal,Scalar>::subViewConst(const Teuchos::ArrayView<const Teuchos_Index> &cols) const
  {
    using Teuchos::as;
    const Ordinal numVecs = cols.size();
    Teuchos::RCP<MultiVectorData<Ordinal,Scalar> > mvdata = Teuchos::rcp( new MultiVectorData<Ordinal,Scalar>() );
    mvdata->constantStride_ = false;
    mvdata->stride_ = stride();
    mvdata->values_ = MVData_->values_;
    mvdata->pointers_ = Teuchos::arcp<Teuchos::ArrayRCP<Scalar> >(numVecs);
    for (Ordinal j = as<Ordinal>(0); j < numVecs; ++j) {
      mvdata->pointers_[j] = MVData_->pointers_[cols[j]];
    }
    Teuchos::RCP<MultiVector<Ordinal,Scalar> > mv = Teuchos::rcp( new MultiVector<Ordinal,Scalar>(this->getMap(),mvdata) );
    return mv;
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractCopy(const Teuchos::ArrayView<Scalar> &A, Ordinal &MyLDA) const 
  {
    TEST_FOR_EXCEPTION(constantStride() == false, std::runtime_error,
      "MultiVector::extractCopy(A,LDA): only supported for constant stride multivectors.");
#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(A.size() != stride()*numVectors(), std::runtime_error,
      "MultiVector::extractCopy(A,LDA): A must be large enough to hold contents of MultiVector.");
#endif
    MyLDA = stride();
    std::copy(MVData_->values_.begin(), MVData_->values_.begin()+stride()*numVectors(),
              A.begin());
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractCopy(Teuchos::ArrayView<Teuchos::ArrayView<Scalar> > arrayOfArrays) const
  {
    (void)arrayOfArrays;
    // FINISH
    TEST_FOR_EXCEPT(true);
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractView(Teuchos::ArrayRCP<Scalar> &A, Ordinal &MyLDA) 
  {
    TEST_FOR_EXCEPTION(constantStride() == false, std::runtime_error,
      "MultiVector::extractConstView(A,LDA): only supported for constant stride multivectors.");
    A = MVData_->values_;
    MyLDA = MVData_->stride_;
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractConstView(Teuchos::ArrayRCP<const Scalar> &A, Ordinal &MyLDA) const
  {
    TEST_FOR_EXCEPTION(constantStride() == false, std::runtime_error,
      "MultiVector::extractConstView(A,LDA): only supported for constant stride multivectors.");
    A = MVData_->values_.getConst();
    MyLDA = MVData_->stride_;
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractView(Teuchos::ArrayRCP<Teuchos::ArrayRCP<Scalar> > &arrayOfArrays)
  {
    // FINISH
    TEST_FOR_EXCEPT(true);
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::extractConstView(Teuchos::ArrayRCP<Teuchos::ArrayRCP<const Scalar> > &arrayOfArrays) const
  {
    TEST_FOR_EXCEPT(true);
  }

  template<typename Ordinal, typename Scalar>
  void MultiVector<Ordinal,Scalar>::multiply(Teuchos::ETransp transA, Teuchos::ETransp transB, const Scalar &alpha, const MultiVector<Ordinal,Scalar> &A, const MultiVector<Ordinal,Scalar> &B, const Scalar &beta) 
  {
    // This routine performs a variety of matrix-matrix multiply operations, interpreting
    // the MultiVector (this-aka C , A and B) as 2D matrices.  Variations are due to
    // the fact that A, B and C can be local replicated or global distributed
    // MultiVectors and that we may or may not operate with the transpose of 
    // A and B.  Possible cases are:
    using Teuchos::NO_TRANS;      // enums
    using Teuchos::TRANS;
    using Teuchos::CONJ_TRANS;
    using Teuchos::null;
    using Teuchos::ScalarTraits;  // traits
    using Teuchos::OrdinalTraits;
    using Teuchos::as;
    using Teuchos::RCP;           // data structures
    using Teuchos::ArrayRCP;
    using Teuchos::Array;
    using Teuchos::ArrayView;
    using Teuchos::rcp;           // initializers for data structures
    using Teuchos::arcp;

    //                                       Num
    //      OPERATIONS                        cases  Notes
    //  1) C(local) = A^X(local) * B^X(local)  4    (X=Trans or Not, No comm needed) 
    //  2) C(local) = A^T(distr) * B  (distr)  1    (2D dot product, replicate C)
    //  3) C(distr) = A  (distr) * B^X(local)  2    (2D vector update, no comm needed)
    //
    // The following operations are not meaningful for 1D distributions:
    //
    // u1) C(local) = A^T(distr) * B^T(distr)  1
    // u2) C(local) = A  (distr) * B^X(distr)  2
    // u3) C(distr) = A^X(local) * B^X(local)  4
    // u4) C(distr) = A^X(local) * B^X(distr)  4
    // u5) C(distr) = A^T(distr) * B^X(local)  2
    // u6) C(local) = A^X(distr) * B^X(local)  4
    // u7) C(distr) = A^X(distr) * B^X(local)  4
    // u8) C(local) = A^X(local) * B^X(distr)  4
    //
    // Total of 32 case (2^5).

    std::string errPrefix("Tpetra::MultiVector::multiply(transOpA,transOpB,A,B): ");

    TEST_FOR_EXCEPTION( ScalarTraits<Scalar>::isComplex && (transA == TRANS || transB == TRANS), std::invalid_argument,
        errPrefix << "non-conjugate transpose not supported for complex types.");
    transA = (transA == NO_TRANS ? NO_TRANS : CONJ_TRANS);
    transB = (transB == NO_TRANS ? NO_TRANS : CONJ_TRANS);

    // Compute effective dimensions, w.r.t. transpose operations on 
    Ordinal A_nrows = (transA==CONJ_TRANS) ? A.numVectors() : A.myLength();
    Ordinal A_ncols = (transA==CONJ_TRANS) ? A.myLength() : A.numVectors();
    Ordinal B_nrows = (transB==CONJ_TRANS) ? B.numVectors() : B.myLength();
    Ordinal B_ncols = (transB==CONJ_TRANS) ? B.myLength() : B.numVectors();

    Scalar beta_local = beta; // local copy of beta; might be reassigned below

    TEST_FOR_EXCEPTION( myLength() != A_nrows || numVectors() != B_ncols || A_ncols != B_nrows, std::runtime_error,
        errPrefix << "dimension of *this, op(A) and op(B) must be consistent.");

    bool A_is_local = !A.isDistributed();
    bool B_is_local = !B.isDistributed();
    bool C_is_local = !this->isDistributed();
    bool Case1 = ( C_is_local &&  A_is_local &&  B_is_local);                                           // Case 1: C(local) = A^X(local) * B^X(local)
    bool Case2 = ( C_is_local && !A_is_local && !B_is_local && transA==CONJ_TRANS && transB==NO_TRANS); // Case 2: C(local) = A^T(distr) * B  (distr)
    bool Case3 = (!C_is_local && !A_is_local &&  B_is_local && transA==NO_TRANS  );                     // Case 3: C(distr) = A  (distr) * B^X(local)

    // Test that we are considering a meaningful cases
    TEST_FOR_EXCEPTION( !Case1 && !Case2 && !Case3, std::runtime_error,
        errPrefix << "multiplication of op(A) and op(B) into *this is not a supported use case.");

    if (beta != ScalarTraits<Scalar>::zero() && Case2) // 
    {
      // if Case2, then C is local and contributions must be summed across all nodes
      // however, if beta != 0, then accumulate beta*C into the sum
      // when summing across all nodes, we only want to accumulate this once, so 
      // set beta == 0 on all nodes except node 0
      int MyPID = this->getMap().getComm()->getRank();
      if (MyPID!=0) beta_local = ScalarTraits<Scalar>::zero();
    }

    // Check if A, B, C have constant stride, if not then make temp copy (strided)
    RCP<const MultiVector<Ordinal,Scalar> > Atmp, Btmp; 
    RCP<MultiVector<Ordinal,Scalar> > Ctmp;
    if (constantStride() == false) Ctmp = rcp(new MultiVector<Ordinal,Scalar>(*this));
    else Ctmp = rcp(this,false);

    if (A.constantStride() == false) Atmp = rcp(new MultiVector<Ordinal,Scalar>(A));
    else Atmp = rcp(&A,false);

    if (B.constantStride() == false) Btmp = rcp(new MultiVector<Ordinal,Scalar>(B));
    else Btmp = rcp(&B,false);

#ifdef TEUCHOS_DEBUG
    TEST_FOR_EXCEPTION(!Ctmp->constantStride() || !Btmp->constantStride() || !Atmp->constantStride(), std::logic_error,
        errPrefix << "failed making temporary strided copies of input multivectors.");
#endif

    Ordinal m = this->myLength();
    Ordinal n = this->numVectors();
    Ordinal k = A_ncols;
    Ordinal lda, ldb, ldc;
    ArrayRCP<const Scalar> Ap, Bp;
    ArrayRCP<Scalar> Cp;
    Atmp->extractConstView(Ap,lda);
    Btmp->extractConstView(Bp,ldb);
    Ctmp->extractView(Cp,ldc);

    Teuchos::BLAS<Ordinal,Scalar> blas;
    // do the arithmetic now
    blas.GEMM(transA,transB,m,n,k,alpha,Ap.getRawPtr(),lda,Bp.getRawPtr(),ldb,beta_local,Cp.getRawPtr(),ldc);

    // Dispose of (possibly) extra copies of A, B
    Atmp = null;
    Btmp = null;

    // If *this was not strided, copy the data from the strided version and then delete it
    if (constantStride() == false) {
      Array<ArrayView<Scalar> > aoa(MVData_->pointers_.size(),null);
      for (Ordinal i=0; i<as<Ordinal>(aoa.size()); ++i) {
        aoa[i] = MVData_->pointers_[i]();
      }
      Ctmp->extractCopy(aoa());
    }
    Ctmp = null;

    // If Case 2 then sum up C and distribute it to all processors.
    if (Case2) 
    {
      RCP<const Teuchos::Comm<Ordinal> > comm = this->getMap().getComm();
      // Global reduction on each entry of a Replicated Local MultiVector
      // Comm requires that local and global buffers be congruous and distinct
      // Therefore, we must allocate storage for the local values
      // Furthermore, if the storage in C (our destination for the global results)
      //   is not packed, we must allocate storage for the result as well.
      ArrayRCP<Scalar> source = arcp<Scalar>(m*n), target;
      bool packed = constantStride() && (stride() == m);
      if (packed) {
        // copy local info into source buffer
        // target buffer will be multivector storage
        std::copy(MVData_->values_.begin(),MVData_->values_.begin()+m*n,
                  source.begin());
        // local storage is packed. can use it for target buffer.
        target = MVData_->values_;
      }
      else {
        // copy local info into source buffer
        ArrayRCP<Scalar> sptr = source;
        for (Ordinal j=OrdinalTraits<Ordinal>::zero(); j<n; ++j) 
        {
          // copy j-th local MV data into source buffer
          std::copy(MVData_->pointers_[j].begin(),MVData_->pointers_[j].begin()+m,
                    sptr.begin());
          // advance ptr into source buffer
          sptr += m;
        }
        // must allocate packed storage for target buffer
        target = arcp<Scalar>(m*n);
      }
      // reduce 
      Teuchos::reduceAll<Ordinal,Scalar>(*comm,Teuchos::REDUCE_SUM,m*n,source.getRawPtr(),target.getRawPtr());
      if (!packed) {
        // copy target buffer into multivector storage buffer
        ArrayRCP<Scalar> tptr = target;
        for (Ordinal j=OrdinalTraits<Ordinal>::zero(); j<n; ++j) 
        {
          std::copy(tptr.begin(),tptr.begin()+m,
                    MVData_->pointers_[j].begin()
                   );
          tptr += m;
        }
      }
      // clear allocated buffers
      source = null;
      target = null;
    } // Case2 reduction
  }



} // namespace Tpetra

#endif // TPETRA_MULTIVECTOR_HPP
