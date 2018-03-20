// Package:    OuterTrackerL1
// Class:      OuterTrackerL1
//
// Original Author:  Isis Marina Van Parijs
// Modified by: Emily MacDonald (emily.kaelyn.macdonald@cern.ch)

// system include files
#include <memory>
#include <vector>
#include <numeric>
#include <iostream>
#include <fstream>

// user include files
#include "CommonTools/Statistics/interface/ChiSquaredProbability.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Ptr.h"
#include "DataFormats/Common/interface/Ref.h"
#include "DataFormats/L1TrackTrigger/interface/TTCluster.h"
#include "DataFormats/L1TrackTrigger/interface/TTStub.h"
#include "DataFormats/L1TrackTrigger/interface/TTTrack.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "DQM/OuterTrackerL1/interface/OuterTrackerMonitorTTTrack.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/CommonDetUnit/interface/GeomDet.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/TrackerGeometryBuilder/interface/StripGeomDetUnit.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimTracker/TrackTriggerAssociation/interface/TTStubAssociationMap.h"
#include "SimTracker/TrackTriggerAssociation/interface/TTTrackAssociationMap.h"
#include "SimTracker/TrackTriggerAssociation/interface/TTClusterAssociationMap.h"

// constructors and destructor
OuterTrackerMonitorTTTrack::OuterTrackerMonitorTTTrack(const edm::ParameterSet& iConfig)
:conf_(iConfig)
{
  topFolderName_ = conf_.getParameter<std::string>("TopFolderName");
  ttTrackToken_ = consumes< std::vector < TTTrack< Ref_Phase2TrackerDigi_ > > > (conf_.getParameter<edm::InputTag>("TTTracksTag"));
  HQNStubs_        = conf_.getParameter<int>("HQNStubs");
  HQChi2dof_       = conf_.getParameter<double>("HQChi2dof");
}

OuterTrackerMonitorTTTrack::~OuterTrackerMonitorTTTrack()
{
  // do anything here that needs to be done at desctruction time
  // (e.g. close files, deallocate resources etc.)
}

// member functions

// ------------ method called for each event  ------------
void OuterTrackerMonitorTTTrack::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup)
{
  // L1 Primaries
  edm::Handle< std::vector< TTTrack< Ref_Phase2TrackerDigi_ > > > TTTrackHandle;
  iEvent.getByToken(ttTrackToken_, TTTrackHandle);

  /// Track Trigger Tracks
  unsigned int numHQTracks = 0;
  unsigned int numLQTracks = 0;
  unsigned int numTracks = 0;

  // Adding protection
  if ( !TTTrackHandle.isValid() ) return;
  //  if ( TTTrackHandle.isValid() ) return;

  /// Go on only if there are TTTracks from Phase2TrackerDigis
  if (!TTTrackHandle->empty())
  {
    /// Loop over TTTracks
    unsigned int tkCnt = 0;
    std::vector< TTTrack< Ref_Phase2TrackerDigi_ > >::const_iterator iterTTTrack;
    for (iterTTTrack = TTTrackHandle->begin();iterTTTrack != TTTrackHandle->end();++iterTTTrack)
    {
      edm::Ptr< TTTrack< Ref_Phase2TrackerDigi_ > > tempTrackPtr(TTTrackHandle, tkCnt++); /// Make the pointer
      numTracks++;

      unsigned int nStubs = tempTrackPtr->getStubRefs().size();
      int nBarrelStubs = 0;
      int nECStubs = 0;

      double trackPt = tempTrackPtr->getMomentum().perp();
      double trackPhi = tempTrackPtr->getMomentum().phi();
      double trackEta = tempTrackPtr->getMomentum().eta();
      double trackVtxZ = tempTrackPtr->getPOCA().z();
      double track_x0 = tempTrackPtr->getPOCA().x();
      double track_y0 = tempTrackPtr->getPOCA().y();

      double track_d0 = sqrt(track_x0*track_x0 + track_y0*track_y0);
      double trackChi2 = tempTrackPtr->getChi2();
      double trackChi2R = tempTrackPtr->getChi2Red();
      float chi2_prob = ChiSquaredProbability(trackChi2, nStubs);

      Track_NStubs->Fill(nStubs);
      Track_Eta_NStubs->Fill(trackEta, nStubs);

      std::vector< edm::Ref< edmNew::DetSetVector< TTStub< Ref_Phase2TrackerDigi_ > >, TTStub< Ref_Phase2TrackerDigi_ > > >  theStubs = iterTTTrack -> getStubRefs() ;
      for (unsigned int istub=0; istub<(unsigned int)theStubs.size(); istub++) {
        bool inTIB = false;
        bool inTOB = false;
        bool inTEC = false;
        bool inTID = false;

        // Verify if stubs are in EC or barrel
        DetId detId(theStubs.at(istub)->getDetId());
        if (detId.det() == DetId::Detector::Tracker) {

          if      (detId.subdetId() == StripSubdetector::TIB) inTIB=true;
          else if (detId.subdetId() == StripSubdetector::TOB) inTOB=true;
          else if (detId.subdetId() == StripSubdetector::TEC) inTEC=true;
          else if (detId.subdetId() == StripSubdetector::TID) inTID=true;
        }
        if (inTIB) nBarrelStubs++;
        else if (inTOB) nBarrelStubs++;
        else if (inTEC) nECStubs++;
        else if (inTID) nECStubs++;
      } //end loop over stubs

      //HQ tracks: >=5 stubs and chi2/dof < 10
      if (nStubs >= HQNStubs_ && trackChi2R <= HQChi2dof_) {
        numHQTracks++;

        Track_HQ_Pt->Fill(trackPt);
        Track_HQ_Eta->Fill(trackEta);
        Track_HQ_Phi->Fill(trackPhi);
        Track_HQ_VtxZ->Fill(trackVtxZ);
        Track_HQ_Chi2->Fill(trackChi2);
        Track_HQ_Chi2Red->Fill(trackChi2R);
        Track_HQ_D0->Fill(track_d0);
        Track_HQ_Chi2Red_NStubs->Fill(nStubs, trackChi2R);
        Track_HQ_Chi2Red_Eta->Fill(trackEta, trackChi2R);
        Track_HQ_Eta_BarrelStubs->Fill(trackEta, nBarrelStubs);
        Track_HQ_Eta_ECStubs->Fill(trackEta, nECStubs);
        Track_HQ_Chi2_Probability->Fill(chi2_prob);
      }

      //LQ: now defined as all tracks (including HQ tracks)
      numLQTracks++;

      Track_LQ_Pt->Fill(trackPt);
      Track_LQ_Eta->Fill(trackEta);
      Track_LQ_Phi->Fill(trackPhi);
      Track_LQ_VtxZ->Fill(trackVtxZ);
      Track_LQ_Chi2->Fill(trackChi2);
      Track_LQ_Chi2Red->Fill(trackChi2R);
      Track_LQ_D0->Fill(track_d0);
      Track_LQ_Chi2Red_NStubs->Fill(nStubs, trackChi2R);
      Track_LQ_Chi2Red_Eta->Fill(trackEta, trackChi2R);
      Track_LQ_Eta_BarrelStubs->Fill(trackEta, nBarrelStubs);
      Track_LQ_Eta_ECStubs->Fill(trackEta,  nECStubs);
      Track_LQ_Chi2_Probability->Fill(chi2_prob);

    } // End of loop over TTTracks
  } // End TTTracks from pixeldigis

  Track_N->Fill(numTracks);
  Track_HQ_N->Fill(numHQTracks);
  Track_LQ_N->Fill(numLQTracks);
} // end of method

// ------------ method called once each job just before starting event loop  ------------
//Creating all histograms for DQM file output

void OuterTrackerMonitorTTTrack::bookHistograms(DQMStore::IBooker &iBooker, edm::Run const & run, edm::EventSetup const & es) {
  std::string HistoName;

  iBooker.setCurrentFolder(topFolderName_+"/Tracks/");
  //Number of tracks
  edm::ParameterSet psTrack_N =  conf_.getParameter<edm::ParameterSet>("TH1_NTracks");
  HistoName = "Track_N";
  Track_N = iBooker.book1D(HistoName, HistoName,
      psTrack_N.getParameter<int32_t>("Nbinsx"),
      psTrack_N.getParameter<double>("xmin"),
      psTrack_N.getParameter<double>("xmax"));
  Track_N->setAxisTitle("# L1 Tracks", 1);
  Track_N->setAxisTitle("# Events", 2);

  //Number of stubs
  edm::ParameterSet psTrack_NStubs =  conf_.getParameter<edm::ParameterSet>("TH1_NStubs");
  HistoName = "Track_NStubs";
  Track_NStubs = iBooker.book1D(HistoName, HistoName,
      psTrack_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_NStubs.getParameter<double>("xmin"),
      psTrack_NStubs.getParameter<double>("xmax"));
  Track_NStubs->setAxisTitle("# L1 Stubs per L1 Track", 1);
  Track_NStubs->setAxisTitle("# L1 Tracks", 2);

  edm::ParameterSet psTrack_Eta_NStubs =  conf_.getParameter<edm::ParameterSet>("TH2_Track_Eta_NStubs");
  HistoName = "Track_Eta_NStubs";
  Track_Eta_NStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta_NStubs.getParameter<double>("xmin"),
      psTrack_Eta_NStubs.getParameter<double>("xmax"),
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Eta_NStubs.getParameter<double>("ymin"),
      psTrack_Eta_NStubs.getParameter<double>("ymax"));
  Track_Eta_NStubs->setAxisTitle("#eta", 1);
  Track_Eta_NStubs->setAxisTitle("# L1 Stubs", 2);

  /// Low-quality tracks (All tracks, including HQ tracks)
  iBooker.setCurrentFolder(topFolderName_+"/Tracks/LQ");
  // Nb of L1Tracks
  HistoName = "Track_LQ_N";
  Track_LQ_N = iBooker.book1D(HistoName, HistoName,
      psTrack_N.getParameter<int32_t>("Nbinsx"),
      psTrack_N.getParameter<double>("xmin"),
      psTrack_N.getParameter<double>("xmax"));
  Track_LQ_N->setAxisTitle("# L1 Tracks", 1);
  Track_LQ_N->setAxisTitle("# Events", 2);

  //Pt of the tracks
  edm::ParameterSet psTrack_Pt =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Pt");
  HistoName = "Track_LQ_Pt";
  Track_LQ_Pt = iBooker.book1D(HistoName, HistoName,
      psTrack_Pt.getParameter<int32_t>("Nbinsx"),
      psTrack_Pt.getParameter<double>("xmin"),
      psTrack_Pt.getParameter<double>("xmax"));
  Track_LQ_Pt->setAxisTitle("p_{T} [GeV]", 1);
  Track_LQ_Pt->setAxisTitle("# L1 Tracks", 2);

  //Phi
  edm::ParameterSet psTrack_Phi =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Phi");
  HistoName = "Track_LQ_Phi";
  Track_LQ_Phi = iBooker.book1D(HistoName, HistoName,
      psTrack_Phi.getParameter<int32_t>("Nbinsx"),
      psTrack_Phi.getParameter<double>("xmin"),
      psTrack_Phi.getParameter<double>("xmax"));
  Track_LQ_Phi->setAxisTitle("#phi", 1);
  Track_LQ_Phi->setAxisTitle("# L1 Tracks", 2);

  //D0
  edm::ParameterSet psTrack_D0 =  conf_.getParameter<edm::ParameterSet>("TH1_Track_D0");
  HistoName = "Track_LQ_D0";
  Track_LQ_D0 = iBooker.book1D(HistoName, HistoName,
      psTrack_D0.getParameter<int32_t>("Nbinsx"),
      psTrack_D0.getParameter<double>("xmin"),
      psTrack_D0.getParameter<double>("xmax"));
  Track_LQ_D0->setAxisTitle("Track D0", 1);
  Track_LQ_D0->setAxisTitle("# L1 Tracks", 2);

  //Eta
  edm::ParameterSet psTrack_Eta =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Eta");
  HistoName = "Track_LQ_Eta";
  Track_LQ_Eta = iBooker.book1D(HistoName, HistoName,
      psTrack_Eta.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta.getParameter<double>("xmin"),
      psTrack_Eta.getParameter<double>("xmax"));
  Track_LQ_Eta->setAxisTitle("#eta", 1);
  Track_LQ_Eta->setAxisTitle("# L1 Tracks", 2);

  //VtxZ
  edm::ParameterSet psTrack_VtxZ =  conf_.getParameter<edm::ParameterSet>("TH1_Track_VtxZ");
  HistoName = "Track_LQ_VtxZ";
  Track_LQ_VtxZ = iBooker.book1D(HistoName, HistoName,
      psTrack_VtxZ.getParameter<int32_t>("Nbinsx"),
      psTrack_VtxZ.getParameter<double>("xmin"),
      psTrack_VtxZ.getParameter<double>("xmax"));
  Track_LQ_VtxZ->setAxisTitle("L1 Track vertex position z [cm]", 1);
  Track_LQ_VtxZ->setAxisTitle("# L1 Tracks", 2);

  //chi2
  edm::ParameterSet psTrack_Chi2 =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Chi2");
  HistoName = "Track_LQ_Chi2";
  Track_LQ_Chi2 = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2.getParameter<double>("xmin"),
      psTrack_Chi2.getParameter<double>("xmax"));
  Track_LQ_Chi2->setAxisTitle("L1 Track #chi^{2}", 1);
  Track_LQ_Chi2->setAxisTitle("# L1 Tracks", 2);

  //chi2Red
  edm::ParameterSet psTrack_Chi2Red =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Chi2R");
  HistoName = "Track_LQ_Chi2Red";
  Track_LQ_Chi2Red = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2Red.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2Red.getParameter<double>("xmin"),
      psTrack_Chi2Red.getParameter<double>("xmax"));
  Track_LQ_Chi2Red->setAxisTitle("L1 Track #chi^{2}/ndf", 1);
  Track_LQ_Chi2Red->setAxisTitle("# L1 Tracks", 2);

  //Chi2 prob
  edm::ParameterSet psTrack_Chi2_Probability =  conf_.getParameter<edm::ParameterSet>("TH1_Track_Chi2_Probability");
  HistoName = "Track_LQ_Chi2_Probability";
  Track_LQ_Chi2_Probability = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2_Probability.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2_Probability.getParameter<double>("xmin"),
      psTrack_Chi2_Probability.getParameter<double>("xmax"));
  Track_LQ_Chi2_Probability->setAxisTitle("#chi^{2} probability", 1);
  Track_LQ_Chi2_Probability->setAxisTitle("# L1 Tracks", 2);

  //Reduced chi2 vs #stubs
  edm::ParameterSet psTrack_Chi2Red_NStubs =  conf_.getParameter<edm::ParameterSet>("TH2_Track_Chi2R_NStubs");
  HistoName = "Track_LQ_Chi2Red_NStubs";
  Track_LQ_Chi2Red_NStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Chi2Red_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2Red_NStubs.getParameter<double>("xmin"),
      psTrack_Chi2Red_NStubs.getParameter<double>("xmax"),
      psTrack_Chi2Red_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Chi2Red_NStubs.getParameter<double>("ymin"),
      psTrack_Chi2Red_NStubs.getParameter<double>("ymax"));
  Track_LQ_Chi2Red_NStubs->setAxisTitle("# L1 Stubs", 1);
  Track_LQ_Chi2Red_NStubs->setAxisTitle("L1 Track #chi^{2}/ndf", 2);

  //chi2/dof vs eta
  edm::ParameterSet psTrack_Chi2R_Eta =  conf_.getParameter<edm::ParameterSet>("TH2_Track_Chi2R_Eta");
  HistoName = "Track_LQ_Chi2Red_Eta";
  Track_LQ_Chi2Red_Eta = iBooker.book2D(HistoName, HistoName,
      psTrack_Chi2R_Eta.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2R_Eta.getParameter<double>("xmin"),
      psTrack_Chi2R_Eta.getParameter<double>("xmax"),
      psTrack_Chi2R_Eta.getParameter<int32_t>("Nbinsy"),
      psTrack_Chi2R_Eta.getParameter<double>("ymin"),
      psTrack_Chi2R_Eta.getParameter<double>("ymax"));
  Track_LQ_Chi2Red_Eta->setAxisTitle("#eta", 1);
  Track_LQ_Chi2Red_Eta->setAxisTitle("L1 Track #chi^{2}/ndf", 2);

  //Eta vs #stubs in barrel
  HistoName = "Track_LQ_Eta_BarrelStubs";
  Track_LQ_Eta_BarrelStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta_NStubs.getParameter<double>("xmin"),
      psTrack_Eta_NStubs.getParameter<double>("xmax"),
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Eta_NStubs.getParameter<double>("ymin"),
      psTrack_Eta_NStubs.getParameter<double>("ymax"));
  Track_LQ_Eta_BarrelStubs->setAxisTitle("#eta", 1);
  Track_LQ_Eta_BarrelStubs->setAxisTitle("# L1 Barrel Stubs", 2);

  //Eta vs #stubs in EC
  HistoName = "Track_LQ_Eta_ECStubs";
  Track_LQ_Eta_ECStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta_NStubs.getParameter<double>("xmin"),
      psTrack_Eta_NStubs.getParameter<double>("xmax"),
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Eta_NStubs.getParameter<double>("ymin"),
      psTrack_Eta_NStubs.getParameter<double>("ymax"));
  Track_LQ_Eta_ECStubs->setAxisTitle("#eta", 1);
  Track_LQ_Eta_ECStubs->setAxisTitle("# L1 EC Stubs", 2);

  /// High-quality tracks (Now defined as >=5 stubs and chi2/dof < 10)
  iBooker.setCurrentFolder(topFolderName_+"/Tracks/HQ");
  // Nb of L1Tracks
  HistoName = "Track_HQ_N";
  Track_HQ_N = iBooker.book1D(HistoName, HistoName,
      psTrack_N.getParameter<int32_t>("Nbinsx"),
      psTrack_N.getParameter<double>("xmin"),
      psTrack_N.getParameter<double>("xmax"));
  Track_HQ_N->setAxisTitle("# L1 Tracks", 1);
  Track_HQ_N->setAxisTitle("# Events", 2);

  //Pt of the tracks
  HistoName = "Track_HQ_Pt";
  Track_HQ_Pt = iBooker.book1D(HistoName, HistoName,
      psTrack_Pt.getParameter<int32_t>("Nbinsx"),
      psTrack_Pt.getParameter<double>("xmin"),
      psTrack_Pt.getParameter<double>("xmax"));
  Track_HQ_Pt->setAxisTitle("p_{T} [GeV]", 1);
  Track_HQ_Pt->setAxisTitle("# L1 Tracks", 2);

  //Phi
  HistoName = "Track_HQ_Phi";
  Track_HQ_Phi = iBooker.book1D(HistoName, HistoName,
      psTrack_Phi.getParameter<int32_t>("Nbinsx"),
      psTrack_Phi.getParameter<double>("xmin"),
      psTrack_Phi.getParameter<double>("xmax"));
  Track_HQ_Phi->setAxisTitle("#phi", 1);
  Track_HQ_Phi->setAxisTitle("# L1 Tracks", 2);

  //D0
  HistoName = "Track_HQ_D0";
  Track_HQ_D0 = iBooker.book1D(HistoName, HistoName,
      psTrack_D0.getParameter<int32_t>("Nbinsx"),
      psTrack_D0.getParameter<double>("xmin"),
      psTrack_D0.getParameter<double>("xmax"));
  Track_HQ_D0->setAxisTitle("Track D0", 1);
  Track_HQ_D0->setAxisTitle("# L1 Tracks", 2);

  //Eta
  HistoName = "Track_HQ_Eta";
  Track_HQ_Eta = iBooker.book1D(HistoName, HistoName,
      psTrack_Eta.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta.getParameter<double>("xmin"),
      psTrack_Eta.getParameter<double>("xmax"));
  Track_HQ_Eta->setAxisTitle("#eta", 1);
  Track_HQ_Eta->setAxisTitle("# L1 Tracks", 2);

  //VtxZ
  HistoName = "Track_HQ_VtxZ";
  Track_HQ_VtxZ = iBooker.book1D(HistoName, HistoName,
      psTrack_VtxZ.getParameter<int32_t>("Nbinsx"),
      psTrack_VtxZ.getParameter<double>("xmin"),
      psTrack_VtxZ.getParameter<double>("xmax"));
  Track_HQ_VtxZ->setAxisTitle("L1 Track vertex position z [cm]", 1);
  Track_HQ_VtxZ->setAxisTitle("# L1 Tracks", 2);

  //chi2
  HistoName = "Track_HQ_Chi2";
  Track_HQ_Chi2 = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2.getParameter<double>("xmin"),
      psTrack_Chi2.getParameter<double>("xmax"));
  Track_HQ_Chi2->setAxisTitle("L1 Track #chi^{2}", 1);
  Track_HQ_Chi2->setAxisTitle("# L1 Tracks", 2);

  //chi2Red
  HistoName = "Track_HQ_Chi2Red";
  Track_HQ_Chi2Red = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2Red.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2Red.getParameter<double>("xmin"),
      psTrack_Chi2Red.getParameter<double>("xmax"));
  Track_HQ_Chi2Red->setAxisTitle("L1 Track #chi^{2}/ndf", 1);
  Track_HQ_Chi2Red->setAxisTitle("# L1 Tracks", 2);

  //Chi2 prob
  HistoName = "Track_HQ_Chi2_Probability";
  Track_HQ_Chi2_Probability = iBooker.book1D(HistoName, HistoName,
      psTrack_Chi2_Probability.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2_Probability.getParameter<double>("xmin"),
      psTrack_Chi2_Probability.getParameter<double>("xmax"));
  Track_HQ_Chi2_Probability->setAxisTitle("#chi^{2} probability", 1);
  Track_HQ_Chi2_Probability->setAxisTitle("# L1 Tracks", 2);

  //Reduced chi2 vs #stubs
  HistoName = "Track_HQ_Chi2Red_NStubs";
  Track_HQ_Chi2Red_NStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Chi2Red_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2Red_NStubs.getParameter<double>("xmin"),
      psTrack_Chi2Red_NStubs.getParameter<double>("xmax"),
      psTrack_Chi2Red_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Chi2Red_NStubs.getParameter<double>("ymin"),
      psTrack_Chi2Red_NStubs.getParameter<double>("ymax"));
  Track_HQ_Chi2Red_NStubs->setAxisTitle("# L1 Stubs", 1);
  Track_HQ_Chi2Red_NStubs->setAxisTitle("L1 Track #chi^{2}/ndf", 2);

  //chi2/dof vs eta
  HistoName = "Track_HQ_Chi2Red_Eta";
  Track_HQ_Chi2Red_Eta = iBooker.book2D(HistoName, HistoName,
      psTrack_Chi2R_Eta.getParameter<int32_t>("Nbinsx"),
      psTrack_Chi2R_Eta.getParameter<double>("xmin"),
      psTrack_Chi2R_Eta.getParameter<double>("xmax"),
      psTrack_Chi2R_Eta.getParameter<int32_t>("Nbinsy"),
      psTrack_Chi2R_Eta.getParameter<double>("ymin"),
      psTrack_Chi2R_Eta.getParameter<double>("ymax"));
  Track_HQ_Chi2Red_Eta->setAxisTitle("#eta", 1);
  Track_HQ_Chi2Red_Eta->setAxisTitle("L1 Track #chi^{2}/ndf", 2);

  //eta vs #stubs in barrel
  HistoName = "Track_HQ_Eta_BarrelStubs";
  Track_HQ_Eta_BarrelStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta_NStubs.getParameter<double>("xmin"),
      psTrack_Eta_NStubs.getParameter<double>("xmax"),
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Eta_NStubs.getParameter<double>("ymin"),
      psTrack_Eta_NStubs.getParameter<double>("ymax"));
  Track_HQ_Eta_BarrelStubs->setAxisTitle("#eta", 1);
  Track_HQ_Eta_BarrelStubs->setAxisTitle("# L1 Barrel Stubs", 2);

  //eta vs #stubs in EC
  HistoName = "Track_HQ_Eta_ECStubs";
  Track_HQ_Eta_ECStubs = iBooker.book2D(HistoName, HistoName,
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsx"),
      psTrack_Eta_NStubs.getParameter<double>("xmin"),
      psTrack_Eta_NStubs.getParameter<double>("xmax"),
      psTrack_Eta_NStubs.getParameter<int32_t>("Nbinsy"),
      psTrack_Eta_NStubs.getParameter<double>("ymin"),
      psTrack_Eta_NStubs.getParameter<double>("ymax"));
  Track_HQ_Eta_ECStubs->setAxisTitle("#eta", 1);
  Track_HQ_Eta_ECStubs->setAxisTitle("# L1 EC Stubs", 2);

}//end of method

DEFINE_FWK_MODULE(OuterTrackerMonitorTTTrack);
