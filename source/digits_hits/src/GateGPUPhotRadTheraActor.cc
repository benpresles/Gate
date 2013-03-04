/*----------------------
  GATE version name: gate_v6

  Copyright (C): OpenGATE Collaboration

  This software is distributed under the terms
  of the GNU Lesser General  Public Licence (LGPL)
  See GATE/LICENSE.txt for further details
  ----------------------*/

#ifndef GateGPUPhotRadTheraActor_CC
#define GateGPUPhotRadTheraActor_CC

#include "GateGPUPhotRadTheraActor.hh"
#include "GateMiscFunctions.hh"
#include "GateRunManager.hh"
#include "GateVImageVolume.hh"
#include "GateRandomEngine.hh"
#include "GateApplicationMgr.hh"

#include "G4Gamma.hh"
#include "G4Electron.hh"

#include <sys/time.h>

//-----------------------------------------------------------------------------
GateGPUPhotRadTheraActor::GateGPUPhotRadTheraActor(G4String name, G4int depth):
  GateVActor(name,depth) {
  GateDebugMessageInc("Actor",4,"GateGPUPhotRadTheraActor() -- begin"<<G4endl);
  gpu_input = 0;
  gpu_output = 0;
  mGPUDeviceID = 0;
  max_buffer_size = 5;
  pMessenger = new GateGPUPhotRadTheraActorMessenger(this);
  GateDebugMessageDec("Actor",4,"GateGPUPhotRadTheraActor() -- end"<<G4endl);
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
/// Destructor 
GateGPUPhotRadTheraActor::~GateGPUPhotRadTheraActor()  {
  delete pMessenger;
  GateGPUIO_Input_delete(gpu_input);
  GateGPUIO_Output_delete(gpu_output);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
/// Construct
void GateGPUPhotRadTheraActor::Construct() {
  GateDebugMessageInc("Actor", 4, "GateGPUPhotRadTheraActor -- Construct - begin" << G4endl);
  GateVActor::Construct();

  // Enable callbacks
  EnableBeginOfRunAction(true);
  EnableBeginOfEventAction(false);
  EnablePreUserTrackingAction(false);
  EnablePostUserTrackingAction(false);
  EnableUserSteppingAction(true);

  ResetData();
  GateMessageDec("Actor", 4, "GateGPUPhotRadTheraActor -- Construct - end" << G4endl);
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
/// Save data
void GateGPUPhotRadTheraActor::SaveData() {
  //FIXME
  DD("SaveData");

  // FIXME DOES NOT WORK
#ifdef GATE_USE_GPU
  GPU_GatePhotRadThera_end(gpu_dosemap, gpu_materials, gpu_phantom);
#endif
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
void GateGPUPhotRadTheraActor::SetGPUDeviceID(int n) {
  mGPUDeviceID = n;
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
void GateGPUPhotRadTheraActor::SetGPUBufferSize(int n) {
  max_buffer_size = n;
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
void GateGPUPhotRadTheraActor::ResetData() {
  DD("ResetData");

  GateGPUIO_Input_delete(gpu_input);
  gpu_input = GateGPUIO_Input_new();

  GateVImageVolume * im = dynamic_cast<GateVImageVolume*>(mVolume);
  G4ThreeVector s = im->GetImage()->GetResolution();
  gpu_input->phantom_size_x = s.x();
  gpu_input->phantom_size_y = s.y();
  gpu_input->phantom_size_z = s.z();

  G4ThreeVector spacing = im->GetImage()->GetVoxelSize();
  gpu_input->phantom_spacing_x = spacing.x();
  gpu_input->phantom_spacing_y = spacing.y();
  gpu_input->phantom_spacing_z = spacing.z();
  
  gpu_input->cudaDeviceID = mGPUDeviceID;
  DD(gpu_input->phantom_size_x);
  DD(gpu_input->phantom_size_y);
  DD(gpu_input->phantom_size_z);
  DD(mGPUDeviceID);

  // Data in unsigned int
  DD(im->GetImage()->end()-im->GetImage()->begin());
  std::vector<float>::const_iterator iter = im->GetImage()->begin();
  while (iter != im->GetImage()->end()) {
    gpu_input->phantom_material_data.push_back((unsigned short int)*iter);
    ++iter;
  }
  DD(gpu_input->phantom_material_data.size());
  
  GateGPUIO_Output_delete(gpu_output);
  gpu_output = GateGPUIO_Output_new();
  DD("end");

  DD(max_buffer_size);

}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
void GateGPUPhotRadTheraActor::BeginOfRunAction(const G4Run *)
{
  DD("GateGPUPhotRadTheraActor::BeginOfRunAction");
  // Set materials
  GateVImageVolume * im = dynamic_cast<GateVImageVolume*>(mVolume);
  std::vector<G4Material*> m;
  im->BuildLabelToG4MaterialVector(m);
  G4String name = im->GetObjectName();
  GateGPUIO_Input_Init_Materials(gpu_input, m, name);
  // Get number of particles
  GateApplicationMgr * a = GateApplicationMgr::GetInstance();
  DD(a->IsTotalAmountOfPrimariesModeEnabled());
  DD(a->GetRequestedAmountOfPrimariesPerRun());
#ifdef GATE_USE_GPU
    GPU_GatePhotRadThera_init(gpu_input, gpu_dosemap, gpu_materials, gpu_phantom);
#endif    
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
void GateGPUPhotRadTheraActor::UserSteppingAction(const GateVVolume * /*v*/, 
                                              const G4Step * step)
{
  GateDebugMessage("Actor", 4, "GateGPUPhotRadTheraActor -- UserSteppingAction" << G4endl);
  
  // Check if we are on the boundary
  G4StepPoint * preStep = step->GetPreStepPoint();
  //  G4StepPoint * postStep = step->GetPostStepPoint();
  if (preStep->GetStepStatus() != fGeomBoundary) { 
    // This is not the first step in the volume 
    step->GetTrack()->SetTrackStatus( fStopAndKill ); // FIXME : one step more to remove.
    return;
  }

  // STEP1 ---------------------------------
  // Store a particle
  GateGPUIO_Particle p;
  // DD(step->GetTrack()->GetDefinition()->GetParticleName());
  if (step->GetTrack()->GetDefinition() == G4Gamma::Gamma()) p.type = 22;       // G4_gamma
  if (step->GetTrack()->GetDefinition() == G4Electron::Electron()) p.type = 11; // G4_electron

  // We dont store e- yet
  if (p.type == 11) {
    // DD("stop because e-");
    return;
  }

  p.E = preStep->GetKineticEnergy()/MeV;
  p.eventID = GateRunManager::GetRunManager()->GetCurrentEvent()->GetEventID();
  p.trackID = step->GetTrack()->GetTrackID();
  p.t = preStep->GetGlobalTime();

  int h = preStep->GetTouchable()->GetHistory()->GetDepth ();
  // DD(h);
  const G4AffineTransform transformation = 
    preStep->GetTouchable()->GetHistory()->GetTransform(h-3);

  //FIXME : store transformation with phantom centre and explain why -3

  //  preStep->GetTouchable()->GetHistory()->GetTopTransform().Inverse();  
  // DD(transformation.NetTranslation());

  G4ThreeVector zero;
  // DD(transformation.TransformPoint(zero));

  G4ThreeVector localPosition = transformation.TransformPoint(preStep->GetPosition());
  // DD(preStep->GetPosition());
  //  DD(localPosition);
  
  p.px = localPosition.x();
  p.py = localPosition.y();
  p.pz = localPosition.z();

  // FIXME Apply rotation to momentum

  p.dx = preStep->GetMomentumDirection().x();
  p.dy = preStep->GetMomentumDirection().y();
  p.dz = preStep->GetMomentumDirection().z();
  //  GateGPUIO_Particle_Print(p);

  gpu_input->particles.push_back(p); // FIXME SLOW 

  // We kill the particle without mercy
  step->GetTrack()->SetTrackStatus( fStopAndKill );

  //DD(gpu_input->particles.size());

  // FIXME If there are less than max_buffer_size the GPU will nerver proceed particles
  // STEP2 if enough particles in the buffer, start the gpu tracking
  if (gpu_input->particles.size() == max_buffer_size) {
    
    // DD(max_buffer_size);
    gpu_input->seed = static_cast<unsigned int>(*GateRandomEngine::GetInstance()->GetRandomEngine());
    DD(gpu_input->seed);
#ifdef GATE_USE_GPU
    GPU_GatePhotRadThera(gpu_input, gpu_output, gpu_dosemap, gpu_materials, gpu_phantom);
#endif    

    // FIXME Dosemap

    // Free output
    gpu_output->particles.clear();
    gpu_input->particles.clear();
  }


}
//-----------------------------------------------------------------------------


#endif /* end #define GateGPUPhotRadTheraActor_CC */
