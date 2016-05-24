/*
// @HEADER
// ***********************************************************************
//
//          Tpetra: Templated Linear Algebra Services Package
//                 Copyright (2008) Sandia Corporation
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
// ************************************************************************
// @HEADER
*/

#include <Tpetra_ConfigDefs.hpp>
#include "Teuchos_UnitTestHarness.hpp"
#include <Tpetra_TestingUtilities.hpp>
#include <Teuchos_OrdinalTraits.hpp>
#include <Teuchos_as.hpp>
#include <Teuchos_Tuple.hpp>
#include "Tpetra_ConfigDefs.hpp"
#include "Tpetra_DefaultPlatform.hpp"
#include "Tpetra_Map.hpp"
#include "Tpetra_Import.hpp"
#include "Tpetra_Export.hpp"
#include "Tpetra_MultiVector.hpp"
#include "Tpetra_Vector.hpp"
#include <iterator>

#include <Tpetra_Distributor.hpp>
#include "Teuchos_FancyOStream.hpp"
#include "Tpetra_Experimental_BlockCrsMatrix.hpp"
#include "Tpetra_Experimental_BlockCrsMatrix_Helpers.hpp"
#include "Tpetra_CrsGraph.hpp"
#include "Tpetra_CrsMatrix.hpp"
#include "Teuchos_FancyOStream.hpp"
#include "Teuchos_GlobalMPISession.hpp"
#include "Teuchos_oblackholestream.hpp"

namespace {
  using Tpetra::TestingUtilities::getNode;

  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::outArg;
  using Tpetra::DefaultPlatform;
  using Tpetra::global_size_t;
  using std::vector;
  using std::sort;
  using Teuchos::arrayViewFromVector;
  using Teuchos::broadcast;
  using Teuchos::OrdinalTraits;
  using Teuchos::tuple;
  using Teuchos::Range1D;
  using Tpetra::Map;
  using Tpetra::Import;
  using Tpetra::Export;
  using Teuchos::ScalarTraits;
  using Teuchos::Comm;
  using Teuchos::Array;
  using Tpetra::REPLACE;
  using Tpetra::ADD;
  using std::ostream_iterator;
  using std::endl;

  using Tpetra::createContigMap;
  using Tpetra::createContigMapWithNode;

  bool testMpi = true;
  double errorTolSlack = 1e+1;

  TEUCHOS_STATIC_SETUP()
  {
    Teuchos::CommandLineProcessor &clp = Teuchos::UnitTestRepository::getCLP();
    clp.addOutputSetupOptions(true);
    clp.setOption(
        "test-mpi", "test-serial", &testMpi,
        "Test MPI (if available) or force test of serial.  In a serial build,"
        " this option is ignored and a serial comm is always used." );
    clp.setOption(
        "error-tol-slack", &errorTolSlack,
        "Slack off of machine epsilon used to check test results" );
  }

  RCP<const Comm<int> > getDefaultComm()
  {
    if (testMpi) {
      return DefaultPlatform::getDefaultPlatform().getComm();
    }
    return rcp(new Teuchos::SerialComm<int>());
  }

  //
  // UNIT TESTS
  //
  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(ImportExport,ImportConstructExpert,LO,GO,NT) {
    // 
    using Teuchos::RCP;
    using Teuchos::rcp;
    using std::endl;
    typedef Teuchos::Array<int>::size_type size_type;
    typedef Tpetra::Experimental::BlockCrsMatrix<double,LO,GO,NT> matrix_type;
    typedef typename matrix_type::impl_scalar_type Scalar;
    typedef Tpetra::Map<LO,GO,NT> map_type;
    typedef Tpetra::CrsGraph<LO,GO,NT>  graph_type;
    typedef Tpetra::global_size_t GST;
    typedef typename matrix_type::device_type device_type; 
    typedef typename Kokkos::View<Scalar**, Kokkos::LayoutRight, device_type>::HostMirror block_type;

    Teuchos::OSTab tab0 (out);
    Teuchos::OSTab tab1 (out);

    RCP<const Comm<int> > comm = getDefaultComm();

    int rank = comm->getRank();

    const LO NumRows = 32;
    const GST gblNumRows = static_cast<GST> ( NumRows * comm->getSize ());
    const GO indexBase = 0;
    const size_t numEntPerRow = 11;

    RCP<const map_type> rowMap =
      rcp (new map_type (gblNumRows, static_cast<size_t> (NumRows),
                         indexBase, comm));
    const GO gblNumCols = static_cast<GO> (rowMap->getGlobalNumElements ());

    RCP<graph_type> G =
      rcp (new graph_type (rowMap, numEntPerRow,
                           Tpetra::StaticProfile));

    Teuchos::Array<GO> gblColInds (numEntPerRow);
    for (LO lclRow = 0; lclRow < NumRows; ++lclRow) { 
      const GO gblInd = rowMap->getGlobalElement (lclRow);
      // Put some entries in the graph. 
      for (LO k = 0; k < static_cast<LO> (numEntPerRow); ++k) {
        const GO curColInd = (gblInd + static_cast<GO> (3*k)) % gblNumCols;
        gblColInds[k] = curColInd;
      }
      G->insertGlobalIndices (gblInd, gblColInds ());
    }
    // Make the graph ready for use byCrsMatrix.
    G->fillComplete ();
    const auto& meshRowMap = * (G->getRowMap ());
    // Contrary to expectations, asking for the graph's number of
    // columns, or asking the column Map for the number of entries,
    // won't give the correct number of columns in the graph.
    // const GO gblNumCols = graph->getDomainMap ()->getGlobalNumElements ();
    const LO lclNumRows = meshRowMap.getNodeNumElements ();
    const LO blkSize = 16;

    RCP<matrix_type> A = rcp (new matrix_type (*G, blkSize));
    // Create a values to use when filling the sparse matrix. Use primes because it's cute.
    Scalar prime[]={
      2,    3,   5,   7,  11,   13,  17,  19,  23,  29,
      31,  37,  41,  43,  47,   53,  59,  61,  67,  71, 
      73,  79,  83,  89,  97,  101, 103, 107, 109, 113, 
      127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 
      179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 
      233, 239, 241, 251, 257, 263, 269, 271};
    int idx=0;
    block_type curBlk ("curBlk", blkSize, blkSize);
    
    for (LO j = 0; j < blkSize; ++j) {
      for (LO i = 0; i < blkSize; ++i) {
        curBlk(i,j) = 1.0* prime[idx++];;
	if (idx >=58) idx = 0;
      }
    }

    // Fill in the block sparse matrix.
    for (LO lclRow = 0; lclRow < lclNumRows; ++lclRow) { // for each of my rows
      Teuchos::ArrayView<const LO> lclColInds;
      G->getLocalRowView (lclRow, lclColInds);

      // Put some entries in the matrix.
      for (LO k = 0; k < static_cast<LO> (lclColInds.size ()); ++k) {
        const LO lclColInd = lclColInds[k];
        const LO err =
          A->replaceLocalValues (lclRow, &lclColInd, curBlk.ptr_on_device (), 1);
        TEUCHOS_TEST_FOR_EXCEPTION(err != 1, std::logic_error, "Bug");
      }
    }
    

    auto olddist = G->getImporter()->getDistributor();
    
    Teuchos::RCP<const map_type> source = G->getImporter()->getSourceMap ();
    Teuchos::RCP<const map_type> target = G->getImporter()->getTargetMap ();

    // Still need to get remote GID's, and export LID's. 

    Teuchos::ArrayView<const GO> avremoteGIDs = target->getNodeElementList ();
    Teuchos::ArrayView<const GO> avexportGIDs  = source->getNodeElementList ();

    Teuchos::Array<LO> exportLIDs(avexportGIDs.size(),0);
    Teuchos::Array<LO> remoteLIDs(avremoteGIDs.size(),0);
    Teuchos::Array<int> userExportPIDs(avexportGIDs.size(),0);
    Teuchos::Array<int> userRemotePIDs(avremoteGIDs.size(),0);

    source->getRemoteIndexList(avexportGIDs,userExportPIDs,exportLIDs);
    target->getRemoteIndexList(avremoteGIDs,userRemotePIDs,remoteLIDs);

    Teuchos::Array<LO> saveremoteLIDs = G->getImporter()->getRemoteLIDs();

    Teuchos::Array<GO> remoteGIDs(avremoteGIDs);

    Import<LO,GO,NT>  newimport(source, 
				target,
				userRemotePIDs,
				remoteGIDs, 
				exportLIDs,
				userExportPIDs ,
				false,
				Teuchos::null,
				Teuchos::null ); // plist == null
  
    Teuchos::RCP<const map_type> newsource = newimport.getSourceMap ();
    Teuchos::RCP<const map_type> newtarget = newimport.getTargetMap ();

    Teuchos::ArrayView<const GO> newremoteGIDs = newtarget->getNodeElementList ();
    Teuchos::ArrayView<const GO> newexportGIDs = newsource->getNodeElementList ();

    if(avremoteGIDs.size()!=newremoteGIDs.size()) 
      {
	success = false;
	std::cerr<<"Rank "<<rank<<" oldrGID:: "<<avremoteGIDs<<std::endl;
	std::cerr<<"Rank "<<rank<<" newrGID:: "<<newremoteGIDs<<std::endl;
      }
    else  
      for(size_type i=0;i<avremoteGIDs.size();++i)
	if(avremoteGIDs[i]!=newremoteGIDs[i]) {
	  std::cerr<<"Rank "<<rank<<" @["<<i<<"] oldrgid "<<avremoteGIDs[i]<<" newrgid "<<newremoteGIDs[i]<<std::endl;
	  success = false;
	}

    if(avexportGIDs.size()!=newexportGIDs.size()) {
      success = false;
      std::cerr<<"Rank "<<rank<<" oldeGID:: "<<avexportGIDs<<std::endl;
      std::cerr<<"Rank "<<rank<<" neweGID:: "<<newexportGIDs<<std::endl;
    }
    else  
      for(size_type i=0;i<avexportGIDs.size();++i)
	if(avexportGIDs[i]!=newexportGIDs[i]) {
	  success = false;
	  std::cerr<<"Rank "<<rank<<" @["<<i<<"] oldEgid "<<avexportGIDs[i]<<" newEgid "<<newexportGIDs[i]<<std::endl;
	}

  
    Teuchos::Array<LO> newexportLIDs = newimport.getExportLIDs();
    if(newexportLIDs.size()!=exportLIDs.size()) 
      {
	out <<" newexportLIDs.size does not match exportLIDs.size()"<<endl;
	out <<" oldExportLIDs "<<exportLIDs<<endl;
	out <<" newExportLIDs "<<newexportLIDs<<endl;
	success = false;
      }
    else
      for(size_type i=0;i<exportLIDs.size();++i)
	if(exportLIDs[i]!=newexportLIDs[i]) {
	  out <<" exportLIDs["<<i<<"] ="<<exportLIDs[i]<<" != newexportLIDs[i] = "<<newexportLIDs[i]<<endl;
	  success = false;
	  break;
	}

    Teuchos::Array<LO> newremoteLIDs = newimport.getRemoteLIDs();
    if(newremoteLIDs.size()!=saveremoteLIDs.size()) 
      {
	out <<" newremoteLIDs.size does not match remoteLIDs.size()"<<endl;
	out <<" oldRemoteLIDs "<<saveremoteLIDs<<endl;
	out <<" newRemoteLIDs "<<newremoteLIDs<<endl;
	success = false;
      }
    else
      for(size_type i=0;i<saveremoteLIDs.size();++i)
	if(saveremoteLIDs[i]!=newremoteLIDs[i]) {
	  out <<" remoteLIDs["<<i<<"] ="<<remoteLIDs[i]<<" != newremoteLIDs[i] = "<<newremoteLIDs[i]<<endl;
	  success = false;
	  break;
	}
  
    int globalSuccess_int = -1;
    Teuchos::reduceAll( *comm, Teuchos::REDUCE_SUM, success ? 0 : 1, outArg(globalSuccess_int) );
    TEST_EQUALITY_CONST( globalSuccess_int, 0 );
  }


  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL( ImportExport, basic, LO, GO, NT ) {
    const Tpetra::global_size_t INVALID =
      Teuchos::OrdinalTraits<Tpetra::global_size_t>::invalid ();
    RCP<const Comm<int> > comm = getDefaultComm();
    RCP<NT> node = getNode<NT>();
    // create Maps
    RCP<const Map<LO, GO, NT> > source =
      createContigMapWithNode<LO, GO, NT> (INVALID,10,comm,node);
    RCP<const Map<LO, GO, NT> > target =
      createContigMapWithNode<LO, GO, NT> (INVALID, 5,comm,node);
    // create Import object
    RCP<const Import<LO, GO, NT> > importer =
      Tpetra::createImport<LO, GO, NT> (source, target);

    auto same = importer->getNumSameIDs();
    auto permute = importer->getNumPermuteIDs();
    auto remote = importer->getNumRemoteIDs();
    auto sum = same + permute + remote;
    auto expectedSum = target->getNodeNumElements();
    TEST_EQUALITY( sum, expectedSum );
  }

  TEUCHOS_UNIT_TEST_TEMPLATE_4_DECL( ImportExport, GetNeighborsForward, Scalar, LO, GO, Node )
  {
    // import with the importer to duplicate
    // export with the exporter to add and reduce
    typedef Teuchos::ScalarTraits<Scalar> ST;
    typedef Tpetra::MultiVector<Scalar,LO,GO,Node> MV;
    const global_size_t INVALID = OrdinalTraits<global_size_t>::invalid();
    // get a comm and node
    RCP<const Comm<int> > comm = getDefaultComm();
    RCP<Node> node = getNode<Node>();
    const int numImages = comm->getSize(),
              myImageID = comm->getRank();
    if (numImages < 2) return;
    // create a Map
    const size_t numLocal  = 1,
                 numVectors = 5;
    // my neighbors: myImageID-1, me, myImageID+1
    Array<GO> neighbors;
    if (myImageID != 0) neighbors.push_back(myImageID-1);
    neighbors.push_back(myImageID);
    if (myImageID != numImages-1) neighbors.push_back(myImageID+1);
    // two maps: one has one entries per node, the other is the 1-D neighbors
    RCP<const Map<LO,GO,Node> >
      smap = createContigMapWithNode<LO,GO,Node>(INVALID,numLocal,comm,node),
      tmap = rcp(new Map<LO,GO,Node>(INVALID,neighbors(),0,comm,node) );
    for (size_t tnum=0; tnum < 2; ++tnum) {
      RCP<MV> mvMine, mvWithNeighbors;
      // for tnum=0, these are contiguously allocated multivectors
      // for tnum=1, these are non-contiguous views of multivectors
      if (tnum == 0) {
        mvMine = rcp(new MV(smap,numVectors));
        mvWithNeighbors = rcp(new MV(tmap,numVectors));
      }
      else {
        MV mineParent(smap,2+numVectors),
           neigParent(tmap,2+numVectors);
        TEUCHOS_TEST_FOR_EXCEPTION(numVectors != 5, std::logic_error, "Test assumption broken.");
        mvMine = mineParent.subViewNonConst(tuple<size_t>(0,6,3,4,5));
        mvWithNeighbors = neigParent.subViewNonConst(tuple<size_t>(0,6,3,4,5));
      }
      // mvMine = [myImageID  myImageID+numImages ... myImageID+4*numImages]
      for (size_t j=0; j<numVectors; ++j) {
        mvMine->replaceLocalValue(0,j,static_cast<Scalar>(myImageID + j*numImages));
      }
      // create Import from smap to tmap, Export from tmap to smap, test them
      RCP<const Import<LO,GO,Node> > importer =
        Tpetra::createImport<LO,GO,Node>(smap,tmap);
      RCP<const Export<LO,GO,Node> > exporter =
        Tpetra::createExport<LO,GO,Node>(tmap,smap);
      bool local_success = true;
      // importer testing
      TEST_EQUALITY_CONST( importer->getSourceMap() == smap, true );
      TEST_EQUALITY_CONST( importer->getTargetMap() == tmap, true );
      TEST_EQUALITY( importer->getNumSameIDs(), (myImageID == 0 ? 1 : 0) );
      TEST_EQUALITY( importer->getNumPermuteIDs(), static_cast<size_t>(myImageID == 0 ? 0 : 1) );
      TEST_EQUALITY( importer->getNumExportIDs(), (myImageID == 0 || myImageID == numImages - 1 ? 1 : 2) );
      TEST_EQUALITY( importer->getNumRemoteIDs(), (myImageID == 0 || myImageID == numImages - 1 ? 1 : 2) );
      // exporter testing
      TEST_EQUALITY_CONST( exporter->getSourceMap() == tmap, true );
      TEST_EQUALITY_CONST( exporter->getTargetMap() == smap, true );
      TEST_EQUALITY( importer->getNumSameIDs(), (myImageID == 0 ? 1 : 0) );
      TEST_EQUALITY( exporter->getNumPermuteIDs(), static_cast<size_t>(myImageID == 0 ? 0 : 1) );
      // import neighbors, test their proper arrival
      //                   [ 0    n     2n    3n    4n ]
      // mvWithNeighbors = [...  ....  ....  ....  ....]
      //                   [n-1  2n-1  3n-1  4n-1  5n-1]
      mvWithNeighbors->doImport(*mvMine,*importer,REPLACE);
      if (myImageID == 0) {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)); // me
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(j*numImages)+ST::one()); // neighbor
        }
      }
      else if (myImageID == numImages-1) {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)-ST::one()); // neighbor
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(myImageID+j*numImages));           // me
        }
      }
      else {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)-ST::one()); // neighbor
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(myImageID+j*numImages));           // me
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),2,static_cast<Scalar>(myImageID+j*numImages)+ST::one()); // neighbor
        }
      }

      // export values, test
      mvMine->putScalar(Teuchos::ScalarTraits<Scalar>::zero());
      mvMine->doExport(*mvWithNeighbors,*exporter,ADD);
      if (myImageID == 0 || myImageID == numImages-1) {
        for (size_t j=0; j<numVectors; ++j) {
          // contribution from me and one neighbor: double original value
          TEST_EQUALITY(mvMine->getData(j)[0],static_cast<Scalar>(2.0)*static_cast<Scalar>(myImageID+j*numImages));
        }
      }
      else {
        for (size_t j=0; j<numVectors; ++j) {
          // contribution from me and two neighbors: triple original value
          TEST_EQUALITY(mvMine->getData(j)[0],static_cast<Scalar>(3.0)*static_cast<Scalar>(myImageID+j*numImages));
        }
      }
      success &= local_success;
    }
    // All procs fail if any proc fails
    int globalSuccess_int = -1;
    Teuchos::reduceAll( *comm, Teuchos::REDUCE_SUM, success ? 0 : 1, outArg(globalSuccess_int) );
    TEST_EQUALITY_CONST( globalSuccess_int, 0 );
  }

  TEUCHOS_UNIT_TEST_TEMPLATE_4_DECL( ImportExport, GetNeighborsBackward, Scalar, LO, GO, Node )
  {
    // import with the exporter to duplicate
    // export with the importer to add and reduce
    typedef ScalarTraits<Scalar> ST;
    typedef Tpetra::MultiVector<Scalar,LO,GO,Node> MV;
    const global_size_t INVALID = OrdinalTraits<global_size_t>::invalid();
    RCP<const Comm<int> > comm = getDefaultComm();
    RCP<Node> node = getNode<Node>();
    const int numImages = comm->getSize(),
              myImageID = comm->getRank();
    if (numImages < 2) return;
    // create a Map
    const size_t numLocal = 1,
               numVectors = 5;
    // my neighbors: myImageID-1, me, myImageID+1
    Array<GO> neighbors;
    if (myImageID != 0) neighbors.push_back(myImageID-1);
    neighbors.push_back(myImageID);
    if (myImageID != numImages-1) neighbors.push_back(myImageID+1);
    // two maps: one has one entries per node, the other is the 1-D neighbors
    auto smap = createContigMapWithNode<LO, GO, Node> (INVALID, numLocal, comm, node);
    auto tmap = rcp (new Map<LO, GO, Node> (INVALID, neighbors (), 0, comm, node));
    for (size_t tnum=0; tnum < 2; ++tnum) {
      RCP<MV> mvMine, mvWithNeighbors;
      // for tnum=0, these are contiguously allocated multivectors
      // for tnum=1, these are non-contiguous views of multivectors
      if (tnum == 0) {
        mvMine = rcp(new MV(smap,numVectors));
        mvWithNeighbors = rcp(new MV(tmap,numVectors));
      }
      else {
        MV mineParent(smap,2+numVectors),
           neigParent(tmap,2+numVectors);
        TEUCHOS_TEST_FOR_EXCEPTION(numVectors != 5, std::logic_error, "Test assumption broken.");
        mvMine = mineParent.subViewNonConst(tuple<size_t>(0,6,3,4,5));
        mvWithNeighbors = neigParent.subViewNonConst(tuple<size_t>(0,6,3,4,5));
      }
      // mvMine = [myImageID  myImageID+numImages ... myImageID+4*numImages]
      for (size_t j=0; j<numVectors; ++j) {
        mvMine->replaceLocalValue(0,j,static_cast<Scalar>(myImageID + j*numImages));
      }
      // create Import from smap to tmap, Export from tmap to smap, test them
      auto importer = Tpetra::createImport<LO, GO, Node> (smap, tmap);
      auto exporter = Tpetra::createExport<LO, GO, Node> (tmap, smap);
      bool local_success = true;
      // importer testing
      TEST_EQUALITY_CONST( importer->getSourceMap() == smap, true );
      TEST_EQUALITY_CONST( importer->getTargetMap() == tmap, true );
      TEST_EQUALITY( importer->getNumSameIDs(), (myImageID == 0 ? 1 : 0) );
      TEST_EQUALITY( importer->getNumPermuteIDs(), static_cast<size_t>(myImageID == 0 ? 0 : 1) );
      TEST_EQUALITY( importer->getNumExportIDs(), (myImageID == 0 || myImageID == numImages - 1 ? 1 : 2) );
      TEST_EQUALITY( importer->getNumRemoteIDs(), (myImageID == 0 || myImageID == numImages - 1 ? 1 : 2) );
      // exporter testing
      TEST_EQUALITY_CONST( exporter->getSourceMap() == tmap, true );
      TEST_EQUALITY_CONST( exporter->getTargetMap() == smap, true );
      TEST_EQUALITY( importer->getNumSameIDs(), (myImageID == 0 ? 1 : 0) );
      TEST_EQUALITY( exporter->getNumPermuteIDs(), static_cast<size_t>(myImageID == 0 ? 0 : 1) );
      // import neighbors, test their proper arrival
      //                   [ 0    n     2n    3n    4n ]
      // mvWithNeighbors = [...  ....  ....  ....  ....]
      //                   [n-1  2n-1  3n-1  4n-1  5n-1]
      mvWithNeighbors->doImport(*mvMine,*exporter,REPLACE);
      if (myImageID == 0) {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)); // me
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(j*numImages)+ST::one()); // neighbor
        }
      }
      else if (myImageID == numImages-1) {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)-ST::one()); // neighbor
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(myImageID+j*numImages));           // me
        }
      }
      else {
        for (size_t j=0; j<numVectors; ++j) {
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),0,static_cast<Scalar>(myImageID+j*numImages)-ST::one()); // neighbor
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),1,static_cast<Scalar>(myImageID+j*numImages));           // me
          TEST_ARRAY_ELE_EQUALITY(mvWithNeighbors->getData(j),2,static_cast<Scalar>(myImageID+j*numImages)+ST::one()); // neighbor
        }
      }
      // export values, test
      mvMine->putScalar(Teuchos::ScalarTraits<Scalar>::zero());
      mvMine->doExport(*mvWithNeighbors,*importer,ADD);
      if (myImageID == 0 || myImageID == numImages-1) {
        for (size_t j=0; j<numVectors; ++j) {
          // contribution from me and one neighbor: double original value
          TEST_EQUALITY(mvMine->getData(j)[0],static_cast<Scalar>(2.0)*static_cast<Scalar>(myImageID+j*numImages));
        }
      }
      else {
        for (size_t j=0; j<numVectors; ++j) {
          // contribution from me and two neighbors: triple original value
          TEST_EQUALITY(mvMine->getData(j)[0],static_cast<Scalar>(3.0)*static_cast<Scalar>(myImageID+j*numImages));
        }
      }
      success &= local_success;
    }
    //
    // All procs fail if any proc fails
    int globalSuccess_int = -1;
    Teuchos::reduceAll( *comm, Teuchos::REDUCE_SUM, success ? 0 : 1, outArg(globalSuccess_int) );
    TEST_EQUALITY_CONST( globalSuccess_int, 0 );
  }

  TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL( ImportExport, AbsMax, LO, GO, Node )
  {
    using Tpetra::createContigMapWithNode;
    using Tpetra::createNonContigMapWithNode;

    // test ABSMAX CombineMode
    // test with local and remote entries, as copyAndPermute() and unpackAndCombine() both need to be tested
    typedef Tpetra::Vector<double,LO,GO,Node> Vec;
    const global_size_t INVALID = OrdinalTraits<global_size_t>::invalid();
    RCP<const Comm<int> > comm = getDefaultComm();
    const int numImages = comm->getSize();
    RCP<Node> node = getNode<Node>();
    if (numImages < 2) return;
    // create a Map
    auto smap = createContigMapWithNode<LO, GO, Node> (INVALID, 1, comm, node);
    const GO myOnlyGID = smap->getGlobalElement (0);
    auto dmap = createNonContigMapWithNode<LO, GO, Node> (tuple<GO> (myOnlyGID, (myOnlyGID+1) % numImages), comm, node);
    RCP<Vec> srcVec = rcp (new Vec (smap));
    srcVec->putScalar (-1.0);
    RCP<Vec> dstVec = rcp (new Vec (dmap));
    dstVec->putScalar (-3.0);
    // first item of dstVec is local (w.r.t. srcVec), while the second is remote
    // ergo, during the import:
    // - the first will be over-written (by 1.0) from the source, while
    // - the second will be "combined", i.e., abs(max(1.0,3.0)) = 3.0 from the dest
    auto importer = Tpetra::createImport<LO, GO, Node> (smap, dmap);
    dstVec->doImport (*srcVec,*importer,Tpetra::ABSMAX);
    TEST_COMPARE_ARRAYS( tuple<double>(-1.0,3.0), dstVec->get1dView() )
    // All procs fail if any proc fails
    int globalSuccess_int = -1;
    Teuchos::reduceAll( *comm, Teuchos::REDUCE_SUM, success ? 0 : 1, outArg(globalSuccess_int) );
    TEST_EQUALITY_CONST( globalSuccess_int, 0 );
  }


 TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL( ImportExport, ExportReverse, LO, GO, Node )
  {
    // This test reproduces an issue seen in Github Issue #114.
    // As of time of checkin, this test will fail on CUDA but pass on other platforms.
    // This is intentional
    RCP<const Comm<int> > comm = getDefaultComm();
    Tpetra::global_size_t INVALID = Teuchos::OrdinalTraits<Tpetra::global_size_t>::invalid();
    typedef Tpetra::Map<LO,GO,Node> Tpetra_Map;
    typedef Tpetra::Import<LO,GO,Node> Tpetra_Import;
    typedef Tpetra::Vector<int, LO, GO,Node> IntVector;
    
    int NumProcs = comm->getSize();
    int MyPID    = comm->getRank();
    
    // This problem only works on 4 procs
    if(NumProcs!=4) {TEST_EQUALITY(true,true);return;}
    
    // Problem setup
    int num_per_proc;
    if(MyPID==0) num_per_proc=7;
    else num_per_proc=6;
    
    GO from_gids_p0[7] = {0,1,2,3,4,5,6};
    GO to_gids_p0[7]   = {0,4,8,12,16,20,24};
    
    GO from_gids_p1[6] = {7,8,9,10,11,12};
    GO to_gids_p1[6]   = {1,5,9,13,17,21};
    
    GO from_gids_p2[6] = {13,14,15,16,17,18};
    GO to_gids_p2[6]   = {2,6,10,14,18,22};
    
    GO from_gids_p3[6] = {19,20,21,22,23,24};
    GO to_gids_p3[6]   = {3,7,11,15,19,23};
    
    // Correctness check array
    int who_owns[25];
    for(int i=0; i<7; i++) 
      who_owns[to_gids_p0[i]] = 0;
    for(int i=0; i<6; i++) {
      who_owns[to_gids_p1[i]] = 1;
      who_owns[to_gids_p2[i]] = 2;
      who_owns[to_gids_p3[i]] = 3;
    }
    
    GO *from_ptr, *to_ptr;
    if(MyPID==0)      {from_ptr=&from_gids_p0[0]; to_ptr=&to_gids_p0[0];}
    else if(MyPID==1) {from_ptr=&from_gids_p1[0]; to_ptr=&to_gids_p1[0];}
    else if(MyPID==2) {from_ptr=&from_gids_p2[0]; to_ptr=&to_gids_p2[0];}
    else if(MyPID==3) {from_ptr=&from_gids_p3[0]; to_ptr=&to_gids_p3[0];}
    else exit(-1);
    
    Teuchos::ArrayView<GO> myfromgids(from_ptr,num_per_proc);
    Teuchos::ArrayView<GO> mytogids(to_ptr,num_per_proc);
    
    // FromMap (from.getRowMap() from Zoltan2)
    RCP<Tpetra_Map> FromMap = rcp(new Tpetra_Map(INVALID,myfromgids,0,comm));
    
    // ToMap (tmap from Zoltan2)
    RCP<Tpetra_Map> ToMap = rcp(new Tpetra_Map(INVALID,mytogids,0,comm));
    
    // Importer
    Tpetra_Import Importer(FromMap,ToMap);
    
    // Duplicating what Zoltan2/Tpetra Does
    IntVector FromVector(FromMap);
    IntVector ToVector(ToMap);  
    ToVector.putScalar(MyPID);
    FromVector.putScalar(-666);
    
    FromVector.doExport(ToVector,Importer,Tpetra::REPLACE);
    
    Teuchos::ArrayRCP<const int> f_rcp = FromVector.getData();
    Teuchos::ArrayView<const int> f_view = f_rcp();
    Teuchos::ArrayRCP<const int> t_rcp = ToVector.getData();
    Teuchos::ArrayView<const int> t_view = t_rcp();
    
    // Check the "FromAnswer" answer against who_owns
    bool all_is_well=true;
    for(size_t i=0; i<FromMap->getNodeNumElements(); i++) {
      if(f_view[i] != who_owns[FromMap->getGlobalElement(i)]) {
	std::cerr<<"["<<MyPID<<"] ERROR: Ownership of GID"<<FromMap->getGlobalElement(i)<<" is incorrect!"<<std::endl;
	all_is_well=false;
      }
    }
    TEST_EQUALITY(all_is_well,true);
    
  }

  //
  // INSTANTIATIONS
  //
 

#define UNIT_TEST_3( LO, GO, NT ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( ImportExport,ImportConstructExpert,LO,GO,NT) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( ImportExport, basic, LO, GO, NT ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( ImportExport, AbsMax, LO, GO, NT ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT( ImportExport, ExportReverse, LO, GO, NT)
  
  #define UNIT_TEST_4( SCALAR, LO, GO, NODE )  \
  TEUCHOS_UNIT_TEST_TEMPLATE_4_INSTANT( ImportExport, GetNeighborsForward,  SCALAR, LO, GO, NODE ) \
  TEUCHOS_UNIT_TEST_TEMPLATE_4_INSTANT( ImportExport, GetNeighborsBackward, SCALAR, LO, GO, NODE )


  TPETRA_ETI_MANGLING_TYPEDEFS()

  TPETRA_INSTANTIATE_LGN( UNIT_TEST_3 )

  TPETRA_INSTANTIATE_SLGN( UNIT_TEST_4 )

} // namespace (anonymous)


