/******************************************
 *
 * Interface to CP Electron selection tool(s).
 *
 * M. Milesi (marco.milesi@cern.ch)
 *
 *
 ******************************************/

// c++ include(s):
#include <iostream>
#include <typeinfo>
#include <sstream>

// EL include(s):
#include <EventLoop/Job.h>
#include <EventLoop/StatusCode.h>
#include <EventLoop/Worker.h>

// EDM include(s):
#include "xAODEventInfo/EventInfo.h"
#include "xAODEgamma/ElectronContainer.h"
#include "xAODEgamma/EgammaDefs.h"
#include "xAODTracking/VertexContainer.h"

// package include(s):
#include "xAODAnaHelpers/ElectronSelector.h"
#include "xAODAnaHelpers/HelperClasses.h"
#include "xAODAnaHelpers/HelperFunctions.h"

#include <xAODAnaHelpers/tools/ReturnCheck.h>
#include <xAODAnaHelpers/tools/ReturnCheckConfig.h>

// ROOT include(s):
#include "TEnv.h"
#include "TFile.h"
#include "TSystem.h"
#include "TObjArray.h"
#include "TObjString.h"

// this is needed to distribute the algorithm to the workers
ClassImp(ElectronSelector)


ElectronSelector :: ElectronSelector () {
}

ElectronSelector :: ElectronSelector (std::string name, std::string configName) :
  Algorithm(),
  m_name(name),
  m_configName(configName),
  m_cutflowHist(nullptr),
  m_cutflowHistW(nullptr),
  m_asgElectronIsEMSelector(nullptr),
  m_asgElectronLikelihoodTool(nullptr),
  m_electronIsolationSelectionTool(nullptr)
{
  // Here you put any code for the base initialization of variables,
  // e.g. initialize all pointers to 0.  Note that you should only put
  // the most basic initialization here, since this method will be
  // called on both the submission and the worker node.  Most of your
  // initialization code will go into histInitialize() and
  // initialize().

  Info("ElectronSelector()", "Calling constructor");
}

ElectronSelector::~ElectronSelector() {}

EL::StatusCode  ElectronSelector :: configure ()
{
  Info("configure()", "Configuing ElectronSelector Interface. User configuration read from : %s ", m_configName.c_str());

  m_configName = gSystem->ExpandPathName( m_configName.c_str() );
  RETURN_CHECK_CONFIG( "ElectronSelector::configure()", m_configName);

  TEnv* config = new TEnv(m_configName.c_str());

  // read debug flag from .config file
  m_debug         = config->GetValue("Debug" ,      false );
  m_useCutFlow    = config->GetValue("UseCutFlow",  true);

  // input container to be read from TEvent or TStore
  m_inContainerName  = config->GetValue("InputContainer",  "");

  // name of algo input container comes from - only if running on systematics
  m_inputAlgo               = config->GetValue("InputAlgo",   "");
  m_outputAlgo              = config->GetValue("OutputAlgo",  "ElectronCollection_Sel_Algo");

  // decorate selected objects that pass the cuts
  m_decorateSelectedObjects = config->GetValue("DecorateSelectedObjects", true);
  // additional functionality : create output container of selected objects
  //                            using the SG::View_Element option
  //                            decorrating and output container should not be mutually exclusive
  m_createSelectedContainer = config->GetValue("CreateSelectedContainer", false);
  // if requested, a new container is made using the SG::View_Element option
  m_outContainerName        = config->GetValue("OutputContainer", "");
  m_outAuxContainerName     = m_outContainerName + "Aux."; // the period is very important!

  // if only want to look at a subset of object
  m_nToProcess              = config->GetValue("NToProcess", -1);

  // configurable cuts
  m_pass_max                = config->GetValue("PassMax", -1);
  m_pass_min                = config->GetValue("PassMin", -1);
  m_pT_max                  = config->GetValue("pTMax",  1e8);
  m_pT_min                  = config->GetValue("pTMin",  1e8);
  m_eta_max                 = config->GetValue("etaMax", 1e8);
  m_vetoCrack               = config->GetValue("VetoCrack", true);
  m_d0_max                  = config->GetValue("d0Max", 1e8);
  m_d0sig_max     	    = config->GetValue("d0sigMax", 1e8);
  m_z0sintheta_max     	    = config->GetValue("z0sinthetaMax", 1e8);

  m_doAuthorCut             = config->GetValue("DoAuthorCut", true);
  m_doOQCut                 = config->GetValue("DoOQCut", true);

  m_confDirPID              = config->GetValue("ConfDirPID", "mc15_20150224");
  // likelihood-based PID
  m_doLHPIDcut         = config->GetValue("DoLHPIDCut", false);
  m_LHPID              = config->GetValue("LHPID", "Loose"); // electron PID as defined by LikeEnum enum (default is 1 - loose).
  m_LHOperatingPoint   = config->GetValue("LHOperatingPoint", "ElectronLikelihoodLooseOfflineConfig2015.conf");
  if( m_LHPID != "VeryLoose" &&
      m_LHPID != "Loose"     &&
      m_LHPID != "Medium"    &&
      m_LHPID != "Tight"     &&
      m_LHPID != "VeryTight" &&
      m_LHPID != "LooseRelaxed" ) {
    Error("configure()", "Unknown electron likelihood PID requested %s!",m_LHPID.c_str());
    return EL::StatusCode::FAILURE;
  }
  // cut-based PID
  m_doCutBasedPIDcut         = config->GetValue("DoCutBasedPIDCut", false);
  m_CutBasedPIDMask          = config->GetValue("CutBasedPIDMask", "ElectronLoosePP"); // electron PID bitmask.
  m_PIDName                  = config->GetValue("PIDName", "isEMLoose");               // electron PID bit-def as defined by egammaPID::PID enum (default is isEMLoose ).
  m_CutBasedOperatingPoint   = config->GetValue("CutBasedOperatingPoint", "ElectronIsEMLooseSelectorCutDefs2012.conf");
  if( m_CutBasedPIDMask != "ElectronLoosePP"   &&
      m_CutBasedPIDMask != "ElectronLoose1"    &&
      m_CutBasedPIDMask != "ElectronMediumPP"  &&
      m_CutBasedPIDMask != "ElectronMedium1"   &&
      m_CutBasedPIDMask != "ElectronTightPP"   &&
      m_CutBasedPIDMask != "ElectronTight1"    &&
      m_CutBasedPIDMask != "ElectronLooseHLT"  &&
      m_CutBasedPIDMask != "ElectronMediumHLT" &&
      m_CutBasedPIDMask != "ElectronTightHLT" ) {
    Error("configure()", "Unknown electron cut-based PID bitmask requested %s!",m_CutBasedPIDMask.c_str());
    return EL::StatusCode::FAILURE;
  }

  // isolation stuff
  m_doIsolation             = config->GetValue("DoIsolationCut"   , false     );
  m_useRelativeIso          = config->GetValue("UseRelativeIso"   , true      );
  m_CaloBasedIsoType        = config->GetValue("CaloBasedIsoType" ,	"etcone20");
  m_CaloBasedIsoCut         = config->GetValue("CaloBasedIsoCut"  , 0.05      );
  m_TrackBasedIsoType       = config->GetValue("TrackBasedIsoType",	"ptcone20");
  m_TrackBasedIsoCut        = config->GetValue("TrackBasedIsoCut" , 0.05      );

  // parse and split by comma
  std::string token;

  m_passAuxDecorKeys        = config->GetValue("PassDecorKeys", "");
  std::istringstream ss(m_passAuxDecorKeys);
  while ( std::getline(ss, token, ',') ) {
    m_passKeys.push_back(token);
  }

  m_failAuxDecorKeys        = config->GetValue("FailDecorKeys", "");
  ss.clear();
  ss.str(m_failAuxDecorKeys);
  while ( std::getline(ss, token, ',') ) {
    m_failKeys.push_back(token);
  }

  if( m_inContainerName.empty() ) {
    Error("configure()", "InputContainer is empty!");
    return EL::StatusCode::FAILURE;
  }

  config->Print();
  Info("configure()", "ElectronSelector Interface succesfully configured! ");

  delete config; config = nullptr;

  return EL::StatusCode::SUCCESS;
}


EL::StatusCode ElectronSelector :: setupJob (EL::Job& job)
{
  // Here you put code that sets up the job on the submission object
  // so that it is ready to work with your algorithm, e.g. you can
  // request the D3PDReader service or add output files.  Any code you
  // put here could instead also go into the submission script.  The
  // sole advantage of putting it here is that it gets automatically
  // activated/deactivated when you add/remove the algorithm from your
  // job, which may or may not be of value to you.

  Info("setupJob()", "Calling setupJob");

  job.useXAOD ();
  xAOD::Init( "ElectronSelector" ).ignore(); // call before opening first file

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: histInitialize ()
{
  // Here you do everything that needs to be done at the very
  // beginning on each worker node, e.g. create histograms and output
  // trees.  This method gets called before any input files are
  // connected.

  Info("histInitialize()", "Calling histInitialize");

  if ( m_useCutFlow ) {
    TFile *file     = wk()->getOutputFile ("cutflow");
    m_cutflowHist  = (TH1D*)file->Get("cutflow");
    m_cutflowHistW = (TH1D*)file->Get("cutflow_weighted");
    m_cutflow_bin  = m_cutflowHist->GetXaxis()->FindBin(m_name.c_str());
    m_cutflowHistW->GetXaxis()->FindBin(m_name.c_str());
  }

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: fileExecute ()
{
  // Here you do everything that needs to be done exactly once for every
  // single file, e.g. collect a list of all lumi-blocks processed

  Info("fileExecute()", "Calling fileExecute");

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: changeInput (bool /*firstFile*/)
{
  // Here you do everything you need to do when we change input files,
  // e.g. resetting branch addresses on trees.  If you are using
  // D3PDReader or a similar service this method is not needed.

  Info("changeInput()", "Calling changeInput");

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: initialize ()
{
  // Here you do everything that you need to do after the first input
  // file has been connected and before the first event is processed,
  // e.g. create additional histograms based on which variables are
  // available in the input files.  You can also create all of your
  // histograms and trees in here, but be aware that this method
  // doesn't get called if no events are processed.  So any objects
  // you create here won't be available in the output if you have no
  // input events.

  Info("initialize()", "Initializing ElectronSelector Interface... ");

  m_event = wk()->xaodEvent();
  m_store = wk()->xaodStore();

  Info("initialize()", "Number of events in file: %lld ", m_event->getEntries() );

  if ( configure() == EL::StatusCode::FAILURE ) {
    Error("initialize()", "Failed to properly configure. Exiting." );
    return EL::StatusCode::FAILURE;
  }

  m_numEvent      = 0;
  m_numObject     = 0;
  m_numEventPass  = 0;
  m_weightNumEventPass  = 0;
  m_numObjectPass = 0;

  // tell the selector tools where to find configuration files
  std::string confDir = "ElectronPhotonSelectorTools/offline/" +  m_confDirPID + "/";

  // initialise AsgElectronIsEMSelector (cut-based PID)
  std::string asgeisem_tool_name = std::string("AsgElectronIsEMSelector_") + m_name;
  m_asgElectronIsEMSelector = new AsgElectronIsEMSelector( asgeisem_tool_name.c_str() );
  m_asgElectronIsEMSelector->msg().setLevel( MSG::INFO); // ERROR, VERBOSE, DEBUG, INFO
  RETURN_CHECK("ElectronSelector::initialize()", m_asgElectronIsEMSelector->setProperty("ConfigFile", confDir + m_CutBasedOperatingPoint ), "Failed to set ConfigFile property"); // set the config file that contains the cuts on the shower shapes
  // Apparently this won't be needed at all ...
  // HelperClasses::EnumParser<egammaPID::PID> cutBasedPIDParser;
  // m_asgElectronIsEMSelector->setProperty("PIDName", static_cast<int>(cutBasedPIDParser.parseEnum(m_PIDName)) );
  // only for DC14 w/ 2012 configuration
  unsigned int EMMask = 999;
  if ( m_CutBasedPIDMask == "ElectronLoosePP" ) {
    EMMask = egammaPID::ElectronLoosePP;
  } else if ( m_CutBasedPIDMask == "ElectronMediumPP" ) {
    EMMask = egammaPID::ElectronMediumPP;
  } else if ( m_CutBasedPIDMask == "ElectronTightPP" ) {
    EMMask = egammaPID::ElectronTightPP;
  } else if ( m_CutBasedPIDMask == "ElectronLoose1" ) {
    EMMask = egammaPID::ElectronLoose1;
  } else if ( m_CutBasedPIDMask == "ElectronMedium1" ) {
    EMMask = egammaPID::ElectronMedium1;
  } else if ( m_CutBasedPIDMask == "ElectronTight1" ) {
    EMMask = egammaPID::ElectronTight1;
  } else if ( m_CutBasedPIDMask == "LooseHLT" ) {
    EMMask = egammaPID::ElectronLooseHLT;
  } else if ( m_CutBasedPIDMask == "ElectronMediumHLT" ) {
    EMMask = egammaPID::ElectronMediumHLT;
  } else if ( m_CutBasedPIDMask == "ElectronTightHLT" ) {
    EMMask = egammaPID::ElectronTightHLT;
  } else {
    Error("initialize()", "Unknown electron cut-based PID bitmask requested %s!",m_CutBasedPIDMask.c_str());
    return EL::StatusCode::FAILURE;
  }
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronIsEMSelector->setProperty("isEMMask", EMMask ), "Failed to set isEMMask property");
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronIsEMSelector->initialize(), "Failed to properly initialize AsgElectronIsEMSelector." );

  // initialise AsgElectronLikelihoodTool (likelihood-based PID)
  std::string asgel_tool_name = std::string("AsgElectronLikelihoodTool_") + m_name;
  m_asgElectronLikelihoodTool = new AsgElectronLikelihoodTool( asgel_tool_name.c_str() );
  m_asgElectronLikelihoodTool->msg().setLevel( MSG::INFO); // ERROR, VERBOSE, DEBUG, INFO
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronLikelihoodTool->setProperty("primaryVertexContainer", "PrimaryVertices"), "Failed to set primaryVertexContainer property");
  // m_asgElectronLikelihoodTool->setProperty("inputPDFFileName", "ElectronPhotonSelectorTools/v1/ElectronLikelihoodPdfs.root");
  HelperClasses::EnumParser<LikeEnum::Menu> likelihoodPIDParser;
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronLikelihoodTool->setProperty("ConfigFile", confDir + m_LHOperatingPoint ), "Failed to set ConfigFile property");
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronLikelihoodTool->setProperty("OperatingPoint", static_cast<unsigned int>( likelihoodPIDParser.parseEnum(m_LHPID) ) ), "Failed to set OperatingPoint property");
  RETURN_CHECK( "ElectronSelector::initialize()", m_asgElectronLikelihoodTool->initialize(), "Failed to properly initialize AsgElectronLikelihoodTool." );

  // initialise ElectronIsolationSelectionTool
  std::string eis_tool_name = std::string("ElectronIsolationSelectionTool_") + m_name;
  m_electronIsolationSelectionTool = new CP::ElectronIsolationSelectionTool( eis_tool_name.c_str() );
  m_electronIsolationSelectionTool->msg().setLevel( MSG::INFO); // ERROR, VERBOSE, DEBUG, INFO
  // https://twiki.cern.ch/twiki/bin/view/AtlasProtected/ElectronIsolationSelectionTool
  HelperClasses::EnumParser<xAOD::Iso::IsolationType> isoParser;
  RETURN_CHECK( "ElectronSelector::initialize()", m_electronIsolationSelectionTool->configureCutBasedIsolation( isoParser.parseEnum(m_CaloBasedIsoType),   static_cast<double>(m_CaloBasedIsoCut),  m_useRelativeIso ), "Failed to configure Calo-Based Isolation Cut");
  RETURN_CHECK( "ElectronSelector::initialize()", m_electronIsolationSelectionTool->configureCutBasedIsolation( isoParser.parseEnum(m_TrackBasedIsoType),  static_cast<double>(m_TrackBasedIsoCut), m_useRelativeIso ), "Failed to configure Track-Based Isolation Cut");
  RETURN_CHECK( "ElectronSelector::initialize()", m_electronIsolationSelectionTool->initialize(), "Failed to properly initialize ElectronIsolationSelectionTool." );

  Info("initialize()", "ElectronSelector Interface succesfully initialized!" );

  return EL::StatusCode::SUCCESS;
}

EL::StatusCode ElectronSelector :: execute ()
{
  // Here you do everything that needs to be done on every single
  // events, e.g. read input variables, apply cuts, and fill
  // histograms and trees.  This is where most of your actual analysis
  // code will go.

  if ( m_debug ) { Info("execute()", "Applying Electron Selection... "); }

  // retrieve MC event weight
  const xAOD::EventInfo* eventInfo(nullptr);
  RETURN_CHECK("ElectronSelector::execute()", HelperFunctions::retrieve(eventInfo, "EventInfo", m_event, m_store, m_debug) ,"");

  float mcEvtWeight(1.0);
  if ( eventInfo->isAvailable< float >( "mcEventWeight" ) ) {
    mcEvtWeight = eventInfo->auxdecor< float >( "mcEventWeight" );
  } else {
    Error("execute()", "mcEventWeight is not available as decoration! Aborting" );
    return EL::StatusCode::FAILURE;
  }

  m_numEvent++;

  // did any collection pass the cuts?
  bool eventPass(false);
  bool countPass(true); // for cutflow: count for the 1st collection in the syst container - could be better as should only count for the nominal
  const xAOD::ElectronContainer* inElectrons(nullptr);

  // if input comes from xAOD, or just running one collection,
  // then get the one collection and be done with it
  if( m_inputAlgo.empty() ) {

    // this will be the collection processed - no matter what!!
    RETURN_CHECK("ElectronSelector::execute()", HelperFunctions::retrieve(inElectrons, m_inContainerName, m_event, m_store, m_debug) ,"");

    // create output container (if requested)
    ConstDataVector<xAOD::ElectronContainer>* selectedElectrons(nullptr);
    if ( m_createSelectedContainer ) { selectedElectrons = new ConstDataVector<xAOD::ElectronContainer>(SG::VIEW_ELEMENTS); }

    // find the selected electrons, and return if event passes object selection
    eventPass = executeSelection( inElectrons, mcEvtWeight, countPass, selectedElectrons );

    if ( m_createSelectedContainer) {
      if ( eventPass ) {
        // add ConstDataVector to TStore
        RETURN_CHECK( "ElectronSelector::execute()", m_store->record( selectedElectrons, m_outContainerName ), "Failed to store const data container");
      } else {
        // if the event does not pass the selection, CDV won't be ever recorded to TStore, so we have to delete it!
        delete selectedElectrons; selectedElectrons = nullptr;
      }
    }

  } else { // get the list of systematics to run over

    // get vector of string giving the syst names of the upstream algo from TStore (rememeber: 1st element is a blank string: nominal case!)
    std::vector< std::string >* systNames(nullptr);
    RETURN_CHECK("ElectronSelector::execute()", HelperFunctions::retrieve(systNames, m_inputAlgo, 0, m_store, m_debug) ,"");

    // prepare a vector of the names of CDV containers for usage by downstream algos
    // must be a pointer to be recorded in TStore
    std::vector< std::string >* vecOutContainerNames = new std::vector< std::string >;
    if ( m_debug ) { Info("execute()", " input list of syst size: %i ", static_cast<int>(systNames->size()) ); }

    // loop over systematic sets
    bool eventPassThisSyst(false);
    for ( auto systName : *systNames ) {

      if ( m_debug ) { Info("execute()", " syst name: %s  input container name: %s ", systName.c_str(), (m_inContainerName+systName).c_str() ); }

      RETURN_CHECK("ElectronSelector::execute()", HelperFunctions::retrieve(inElectrons, m_inContainerName + systName, m_event, m_store, m_debug) ,"");

      // create output container (if requested) - one for each systematic
      ConstDataVector<xAOD::ElectronContainer>* selectedElectrons(nullptr);
      if ( m_createSelectedContainer ) { selectedElectrons = new ConstDataVector<xAOD::ElectronContainer>(SG::VIEW_ELEMENTS); }

      // find the selected electrons, and return if event passes object selection
      eventPassThisSyst = executeSelection( inElectrons, mcEvtWeight, countPass, selectedElectrons );

      if ( countPass ) { countPass = false; } // only count objects/events for 1st syst collection in iteration (i.e., nominal)

      if ( eventPassThisSyst ) {
	// save the string of syst set under question if event is passing the selection
	vecOutContainerNames->push_back( systName );
      }

      // if for at least one syst set the event passes selection, this will remain true!
      eventPass = ( eventPass || eventPassThisSyst );

      if ( m_debug ) { Info("execute()", " syst name: %s  output container name: %s ", systName.c_str(), (m_outContainerName+systName).c_str() ); }

      if ( m_createSelectedContainer ) {
        if ( eventPassThisSyst ) {
          // add ConstDataVector to TStore
          RETURN_CHECK( "ElectronSelector::execute()", m_store->record( selectedElectrons, m_outContainerName+systName ), "Failed to store const data container");
        } else {
          // if the event does not pass the selection for this syst, CDV won't be ever recorded to TStore, so we have to delete it!
          delete selectedElectrons; selectedElectrons = nullptr;
        }
      }

    } // close loop over syst sets

    if ( m_debug ) {  Info("execute()", " output list of syst size: %i ", static_cast<int>(vecOutContainerNames->size()) ); }

    // record in TStore the list of systematics names that should be considered down stream
    RETURN_CHECK( "ElectronSelector::execute()", m_store->record( vecOutContainerNames, m_outputAlgo), "Failed to record vector of output container names.");

  }

  // look what do we have in TStore
  if ( m_debug ) { m_store->print(); }

  if( !eventPass ) {
    wk()->skipEvent();
    return EL::StatusCode::SUCCESS;
  }

  return EL::StatusCode::SUCCESS;

}

bool ElectronSelector :: executeSelection ( const xAOD::ElectronContainer* inElectrons, float mcEvtWeight, bool countPass,
					    ConstDataVector<xAOD::ElectronContainer>* selectedElectrons )
{

  const xAOD::VertexContainer* vertices(nullptr);
  RETURN_CHECK("ElectronSelector::execute()", HelperFunctions::retrieve(vertices, "PrimaryVertices", m_event, m_store, m_debug) ,"");
  const xAOD::Vertex *pvx = HelperFunctions::getPrimaryVertex(vertices);

  int nPass(0); int nObj(0);
  for ( auto el_itr : *inElectrons ) { // duplicated of basic loop

    // if only looking at a subset of electrons make sure all are decorated
    if ( m_nToProcess > 0 && nObj >= m_nToProcess ) {
      if ( m_decorateSelectedObjects ) {
        el_itr->auxdecor< char >( "passSel" ) = -1;
      } else {
        break;
      }
      continue;
    }

    nObj++;
    bool passSel = this->PassCuts( el_itr, pvx );
    if ( m_decorateSelectedObjects ) {
      el_itr->auxdecor< char >( "passSel" ) = passSel;
    }

    if ( passSel ) {
      nPass++;
      if ( m_createSelectedContainer ) {
        selectedElectrons->push_back( el_itr );
      }
    }
  }

  // for cutflow: make sure to count passed objects only once (i.e., this flag will be true only for nominal)
  if ( countPass ) {
    m_numObject     += nObj;
    m_numObjectPass += nPass;
  }

  if ( m_debug ) { Info("execute()", "Initial electrons:%i - Selected electrons: %i", nObj , nPass ); }

  // apply event selection based on minimal/maximal requirements on the number of objects per event passing cuts
  if ( m_pass_min > 0 && nPass < m_pass_min ) {
    return false;
  }
  if ( m_pass_max > 0 && nPass > m_pass_max ) {
    return false;
  }

  // for cutflow: make sure to count passed events only once (i.e., this flag will be true only for nominal)
  if ( countPass ){
    m_numEventPass++;
    m_weightNumEventPass += mcEvtWeight;
  }

  return true;
}

EL::StatusCode ElectronSelector :: postExecute ()
{
  // Here you do everything that needs to be done after the main event
  // processing.  This is typically very rare, particularly in user
  // code.  It is mainly used in implementing the NTupleSvc.

  if ( m_debug ) { Info("postExecute()", "Calling postExecute"); }

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: finalize ()
{
  // This method is the mirror image of initialize(), meaning it gets
  // called after the last event has been processed on the worker node
  // and allows you to finish up any objects you created in
  // initialize() before they are written to disk.  This is actually
  // fairly rare, since this happens separately for each worker node.
  // Most of the time you want to do your post-processing on the
  // submission node after all your histogram outputs have been
  // merged.  This is different from histFinalize() in that it only
  // gets called on worker nodes that processed input events.

  Info("finalize()", "Deleting tool instances...");

  if ( m_asgElectronIsEMSelector )        { delete m_asgElectronIsEMSelector; m_asgElectronIsEMSelector = nullptr; }
  if ( m_asgElectronLikelihoodTool )      { delete m_asgElectronLikelihoodTool; m_asgElectronLikelihoodTool = nullptr; }
  if ( m_electronIsolationSelectionTool ) { delete m_electronIsolationSelectionTool; m_electronIsolationSelectionTool = nullptr; }

  return EL::StatusCode::SUCCESS;
}



EL::StatusCode ElectronSelector :: histFinalize ()
{
  // This method is the mirror image of histInitialize(), meaning it
  // gets called after the last event has been processed on the worker
  // node and allows you to finish up any objects you created in
  // histInitialize() before they are written to disk.  This is
  // actually fairly rare, since this happens separately for each
  // worker node.  Most of the time you want to do your
  // post-processing on the submission node after all your histogram
  // outputs have been merged.  This is different from finalize() in
  // that it gets called on all worker nodes regardless of whether
  // they processed input events.

  Info("histFinalize()", "Calling histFinalize");

  if ( m_useCutFlow ) {
    Info("histFinalize()", "Filling cutflow");
    m_cutflowHist ->SetBinContent( m_cutflow_bin, m_numEventPass        );
    m_cutflowHistW->SetBinContent( m_cutflow_bin, m_weightNumEventPass  );
  }

  return EL::StatusCode::SUCCESS;
}

int ElectronSelector :: PassCuts( const xAOD::Electron* electron, const xAOD::Vertex *primaryVertex ) {

  // https://twiki.cern.ch/twiki/bin/view/AtlasProtected/EGammaIdentificationRun2

  float et    = electron->pt();
  float eta   = electron->eta();

  int oq      = static_cast<int>( electron->auxdata<uint32_t>("OQ") & 1446 );

  // https://twiki.cern.ch/twiki/bin/view/AtlasProtected/InDetTrackingDC14

  const xAOD::TrackParticle* tp  = electron->trackParticle();

  float d0_significance = fabs( tp->d0() ) / sqrt(tp->definingParametersCovMatrix()(0,0) );
  float z0sintheta      = ( tp->z0() + tp->vz() - primaryVertex->z() ) * sin( tp->theta() );

  // author cut
  if ( m_doAuthorCut ) {
    if ( !( electron->author(xAOD::EgammaParameters::AuthorElectron) || electron->author(xAOD::EgammaParameters::AuthorAmbiguous) ) ) {
      if ( m_debug ) { Info("execute()", "Electron failed author kinematic cut." ); }
      return 0;
    }
  }
  // Object Quality cut
  if ( m_doOQCut ) {
    if ( !(oq == 0) ) {
      if ( m_debug ) { Info("execute()", "Electron failed Object Quality cut." ); }
      return 0;
    }
  }
  // pT max
  if ( m_pT_max != 1e8 ) {
    if ( et > m_pT_max ) {
      if ( m_debug ) { Info("execute()", "Electron failed pT max cut." ); }
      return 0;
    }
  }
  // pT min
  if ( m_pT_min != 1e8 ) {
    if ( et < m_pT_min ) {
      if ( m_debug ) { Info("execute()", "Electron failed pT min cut." ); }
      return 0;
    }
  }
  // |eta| max
  if ( m_eta_max != 1e8 ) {
    if ( fabs(eta) > m_eta_max ) {
      if ( m_debug ) { Info("execute()", "Electron failed |eta| max cut." ); }
      return 0;
    }
  }
  // |eta| crack veto
  if ( m_vetoCrack ) {
    if ( fabs(eta) > 1.37 && fabs(eta) < 1.52 ) {
      if ( m_debug ) { Info("execute()", "Electron failed |eta| crack veto cut." ); }
      return 0;
    }
  }
  // d0 cut
  if ( !( tp->d0() < m_d0_max ) ) {
      if ( m_debug ) { Info("PassCuts()", "Electron failed d0 cut."); }
      return 0;
  }
  // d0sig cut
  if ( !( d0_significance < m_d0sig_max ) ) {
      if ( m_debug ) { Info("PassCuts()", "Electron failed d0 significance cut."); }
      return 0;
  }
  // z0*sin(theta) cut
  if ( !( fabs(z0sintheta) < m_z0sintheta_max ) ) {
      if ( m_debug ) { Info("execute()", "Electron failed z0*sin(theta) cut." ); }
      return 0;
  }
  // likelihood PID
  if ( m_doLHPIDcut ) {
    if ( ! m_asgElectronLikelihoodTool->accept( *electron ) ) {
        if ( m_debug ) { Info("execute()", "Electron failed likelihood PID cut." ); }
        return 0;
    }
  }
  // cut-based PID
  if ( m_doCutBasedPIDcut ) {
    if ( ! m_asgElectronIsEMSelector->accept( *electron ) ) {
        if ( m_debug ) { Info("execute()", "Electron failed cut-based PID cut." ); }
        return 0;
    }
  }
  // isolation
  if ( m_doIsolation ) {
    if ( ! m_electronIsolationSelectionTool->accept( *electron ) ) {
      if ( m_debug ) { Info("execute()", "Electron failed isolation cut." ); }
      return 0;
    }
  }
  return 1;
}


