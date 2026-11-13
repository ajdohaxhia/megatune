#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include "EditorApi.h"
#include "TuningRules.h"
#include "TuningDsp.h"
#include <vector>
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static juce::Component* findById(juce::Component* root,const juce::String& id){if(root->getComponentID()==id)return root;for(int i=0;i<root->getNumChildComponents();++i)if(auto* c=findById(root->getChildComponent(i),id))return c;return nullptr;}
static void visitChildren(juce::Component* root,const std::function<void(juce::Component*)>& fn){for(int i=0;i<root->getNumChildComponents();++i){auto* c=root->getChildComponent(i);fn(c);visitChildren(c,fn);}}
static std::atomic<bool> countAllocations{false};
static std::atomic<int> allocationCount{0};
void* operator new(std::size_t n){if(countAllocations.load(std::memory_order_relaxed))allocationCount.fetch_add(1,std::memory_order_relaxed);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){if(countAllocations.load(std::memory_order_relaxed))allocationCount.fetch_add(1,std::memory_order_relaxed);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int main(int argc,char** argv){
 if(argc>1&&juce::String(argv[1])=="--soak"){
  juce::ScopedJuceInitialiser_GUI init;
  std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
  std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
  if(!editor)return 1;
  p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;double phase=0;
  for(int i=0;i<50000;++i){
   for(int n=0;n<512;++n){const float v=.2f*std::sin(phase);phase+=juce::MathConstants<double>::twoPi*220/48000;b.setSample(0,n,v);b.setSample(1,n,v);}
   p->processBlock(b,mb);
   if(i%5==0)megatune::tickEditor(*editor);
   if(i%1000==0){
    auto* panel=findById(editor.get(),"history-panel");
    if(panel&&panel->isVisible()){if(auto* c=panel->findChildWithID("history-close"))dynamic_cast<juce::Button*>(c)->onClick();}
    else if(auto* w=findById(editor.get(),"why-button"))dynamic_cast<juce::Button*>(w)->onClick();
   }
   if(i%2000==1000){
    auto* browser=findById(editor.get(),"preset-browser");
    if(browser&&browser->isVisible()){if(auto* l=browser->findChildWithID("preset-load"))dynamic_cast<juce::Button*>(l)->onClick();}
    else if(auto* br=findById(editor.get(),"preset-browse"))dynamic_cast<juce::Button*>(br)->onClick();
   }
   if(i%5000==0){auto shot=editor->createComponentSnapshot(editor->getLocalBounds());juce::ignoreUnused(shot);p->setCurrentProgram((p->getCurrentProgram()+1)%p->getNumPrograms());}
   for(int n=0;n<512;++n)if(!std::isfinite(b.getSample(0,n))||!std::isfinite(b.getSample(1,n)))return 2;
  }
  return 0;
 }
 const float neutral=megaTuneSmoothingSeconds(100,0,1);
 if (!(megaTuneSmoothingSeconds(100,100,1)<neutral && neutral<megaTuneSmoothingSeconds(100,-100,1))) { std::cerr<<"Sustain must speed up positive and slow down negative held notes\n";return 11; }
 if (megaTuneSmoothingSeconds(100,100,0)!=neutral || megaTuneSmoothingSeconds(100,-100,0)!=neutral) return 12;
 // 432 Hz input against a 440 Hz reference/target must read about -31.8 cents,
 // with the marker left of zero. Zero and positive values use the same mapping.
 {const float flat=69.f+12.f*std::log2(432.f/440.f);
  const float negative=megaTuneCentsBetween(flat,69.f);
  if(std::abs(negative+31.8f)>.2f||megaTuneMeterFraction(negative)>=.5f||megaTuneMeterFraction(negative)<.3f)return 60;
  if(std::abs(megaTuneCentsBetween(69.f,69.f))>.001f||std::abs(megaTuneMeterFraction(0.f)-.5f)>.001f)return 61;
  if(megaTuneCentsBetween(69.3f,69.f)<=0.f||megaTuneMeterFraction(30.f)<=.5f)return 62;
  if(std::abs(megaTuneMeterFraction(-200.f))>.001f||std::abs(megaTuneMeterFraction(200.f)-1.f)>.001f)return 63;}
 // Hosts may deliver blocks larger than the initial 8192-sample staging arrays.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  setP("smith",1.f);r->setRateAndBufferSizeDetails(48000,16384);r->prepareToPlay(48000,16384);
  if(megatune::smithBufferCapacity(*r)<16384)return 83;
  juce::AudioBuffer<float> b(2,16384);for(int i=0;i<16384;++i)for(int c=0;c<2;++c)b.setSample(c,i,.2f*std::sin(juce::MathConstants<double>::twoPi*440*i/48000));
  juce::MidiBuffer mb;r->processBlock(b,mb);double energy=0;for(int i=0;i<16384;++i){if(!std::isfinite(b.getSample(0,i)))return 84;energy+=std::abs(b.getSample(0,i));}
  if(energy<1)return 85;}
 // Signalsmith latency must scale when the host changes sample rate.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(1.f);
  r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);const int lat48=r->getLatencySamples();
  r->setRateAndBufferSizeDetails(96000,512);r->prepareToPlay(96000,512);const int lat96=r->getLatencySamples();
  if(lat48<5000||lat96<lat48*1.8)return 86;
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;for(int i=0;i<512;++i){float v=.2f*std::sin(juce::MathConstants<double>::twoPi*440*i/96000);b.setSample(0,i,v);b.setSample(1,i,v);}
  r->processBlock(b,mb);if(!std::isfinite(b.getSample(0,511)))return 91;}
 // A target change outside the stabilizer window is not a "previous target held".
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  for(int n=0;n<12;++n)setP(("note"+juce::String(n)).toRawUTF8(),0.f);setP("note9",1.f);setP("note11",1.f);
  setP("key",0.f);setP("stabilize",1.f);setP("amount",1.f);setP("speed",1.f);setP("range",1.f);
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);double phase=0;
  auto feed=[&](double hz,int blocks){for(int blk=0;blk<blocks;++blk){for(int i=0;i<256;++i){float v=.25f*std::sin(phase);phase+=juce::MathConstants<double>::twoPi*hz/48000;b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);}};
  feed(440,60);if(megatune::historyDecision(*r,megatune::historySequence(*r)-1).target!=69)return 87;
  bool sawNewTarget=false;for(int blk=0;blk<100&&!sawNewTarget;++blk){feed(493.883,1);const auto d=megatune::historyDecision(*r,megatune::historySequence(*r)-1);if(d.target==71){sawNewTarget=true;if(d.reason==1)return 88;}}
  if(!sawNewTarget)return 89;
  feed(493.883,12);if(megatune::historyDecision(*r,megatune::historySequence(*r)-1).reason!=1)return 92;}
 // Once prepared, one analysis window must not allocate on the audio thread.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
  juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,512);for(int i=0;i<512;++i)for(int c=0;c<2;++c)b.setSample(c,i,.2f*std::sin(juce::MathConstants<double>::twoPi*440*i/48000));
  r->processBlock(b,mb);allocationCount=0;countAllocations=true;r->processBlock(b,mb);countAllocations=false;
  if(allocationCount.load()>0)return 90;}
 // Non-finite host input must not poison the delay line or Signalsmith path.
 {auto run=[&](bool smith){std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(smith?1.f:0.f);
   r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
   juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;
   for(int blk=0;blk<40;++blk){for(int i=0;i<512;++i){const float v=blk<2?std::numeric_limits<float>::quiet_NaN():0.f;b.setSample(0,i,v);b.setSample(1,i,blk<2?std::numeric_limits<float>::infinity():0.f);}r->processBlock(b,mb);
     for(int i=0;i<512;++i)if(!std::isfinite(b.getSample(0,i))||!std::isfinite(b.getSample(1,i)))return false;}
   return true;};
  if(!run(false)||!run(true))return 93;}
 // reset() must flush delayed audio without changing the prepared configuration.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::AudioBuffer<float> b(2,256);juce::MidiBuffer mb;b.clear();b.setSample(0,0,.8f);b.setSample(1,0,.8f);r->processBlock(b,mb);
  r->reset();float peak=0;for(int blk=0;blk<24;++blk){b.clear();r->processBlock(b,mb);for(int i=0;i<256;++i)peak=juce::jmax(peak,std::abs(b.getSample(0,i)));}
  if(peak>1.0e-6f)return 94;}
 // Signalsmith must also be allocation-free in steady state.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(1.f);
  r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;for(int i=0;i<512;++i)for(int c=0;c<2;++c)b.setSample(c,i,.2f*std::sin(juce::MathConstants<double>::twoPi*440*i/48000));
  r->processBlock(b,mb);r->processBlock(b,mb);allocationCount=0;countAllocations=true;r->processBlock(b,mb);countAllocations=false;
  if(allocationCount.load()>0)return 95;}
 // Global Bypass must still pass dry audio when Signalsmith is selected.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  for(int n=0;n<12;++n)setP(("note"+juce::String(n)).toRawUTF8(),1.f);setP("smith",1.f);setP("bypass",1.f);setP("amount",1.f);setP("speed",1.f);
  r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;std::vector<float> steady;
  for(int blk=0;blk<300;++blk){for(int i=0;i<512;++i){float v=.25f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*512+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);if(blk>200)for(int i=0;i<512;++i)steady.push_back(b.getSample(0,i));}
  int bestLag=0;double best=-1;for(int lag=100;lag<125;++lag){double cross=0;for(size_t i=0;i+lag<steady.size();++i)cross+=steady[i]*steady[i+lag];if(cross>best){best=cross;bestLag=lag;}}
  if(bestLag<=0)return 96;const double f=48000.0/bestLag;if(std::abs(f-432)>4){std::cerr<<"Signalsmith bypass output "<<f<<" Hz\n";return 96;}}
 // Doubler and width must still apply after Signalsmith.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  setP("smith",1.f);setP("doubler",.65f);setP("width",1.f);
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::AudioBuffer<float> b(2,256);juce::MidiBuffer mb;double difference=0;
  for(int blk=0;blk<120;++blk){for(int i=0;i<256;++i){float v=.25f*std::sin(juce::MathConstants<double>::twoPi*440*(blk*256+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);if(blk>80)for(int i=0;i<256;++i)difference+=std::abs(b.getSample(0,i)-b.getSample(1,i));}
  if(difference<1){std::cerr<<"Signalsmith doubler is silent\n";return 97;}}
 // Host bypass must be latency-aligned with the selected Signalsmith engine.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(1.f);
  r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);const int latency=r->getLatencySamples();
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;int onset=-1;
  for(int blk=0;blk<20&&onset<0;++blk){b.clear();if(blk==0){b.setSample(0,0,1.f);b.setSample(1,0,1.f);}r->processBlockBypassed(b,mb);
   for(int i=0;i<512;++i)if(std::abs(b.getSample(0,i))>1.0e-4f){onset=blk*512+i;break;}}
  if(onset!=latency){std::cerr<<"Signalsmith bypass onset "<<onset<<" != "<<latency<<"\n";return 98;}}
 // Engine, sample-rate and block-size matrix must stay finite.
 {for(double sr:{44100.0,48000.0,96000.0})for(int block:{1,17,512,4096})for(int mode=0;mode<4;++mode){
   std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
   for(int n=0;n<12;++n)setP(("note"+juce::String(n)).toRawUTF8(),1.f);setP("key",0.f);setP("amount",1.f);setP("speed",1.f);
   setP("lowlatency",mode==1?1.f:0.f);setP("engine",mode==2?1.f:0.f);setP("smith",mode==3?1.f:0.f);
   r->setRateAndBufferSizeDetails(sr,block);r->prepareToPlay(sr,block);
   juce::AudioBuffer<float> b(2,block);juce::MidiBuffer mb;double phase=0;
   for(int blk=0;blk<6;++blk){for(int i=0;i<block;++i){const float v=.2f*std::sin(phase);phase+=juce::MathConstants<double>::twoPi*220/sr;b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);
    for(int c=0;c<2;++c)for(int i=0;i<block;++i)if(!std::isfinite(b.getSample(c,i))){std::cerr<<"non-finite output sr="<<sr<<" block="<<block<<" mode="<<mode<<"\n";return 99;}}}}
 // Mono and unexpected extra channels must be safe.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
  juce::AudioBuffer<float> mono(1,512);juce::MidiBuffer mb;for(int i=0;i<512;++i)mono.setSample(0,i,.2f*std::sin(juce::MathConstants<double>::twoPi*220*i/48000));r->processBlock(mono,mb);
  for(int i=0;i<512;++i)if(!std::isfinite(mono.getSample(0,i)))return 100;
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(1.f);
  r->prepareToPlay(48000,512);juce::AudioBuffer<float> four(4,512);four.clear();juce::MidiBuffer m4;r->processBlock(four,m4);
  for(int c=0;c<2;++c)for(int i=0;i<512;++i)if(!std::isfinite(four.getSample(c,i)))return 101;}
 // Negative Speed is the hard-correction path; zero and positive smooth.
 {if(megaTuneRatioBlend(-3.f,0.f,1.f,48000.f)!=1.f)return 102;
  const float soft=megaTuneRatioBlend(25.f,0.f,1.f,48000.f);if(!(soft>0.f&&soft<1.f))return 103;}
 // Shift remains an independent transpose when Amount is zero.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  for(int n=0;n<12;++n)setP(("note"+juce::String(n)).toRawUTF8(),1.f);setP("key",0.f);setP("amount",0.f);
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="shift")if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(p))ranged->setValueNotifyingHost(ranged->convertTo0to1(2.f));
  r->setRateAndBufferSizeDetails(48000,512);r->prepareToPlay(48000,512);
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;std::vector<float> steady;
  for(int blk=0;blk<300;++blk){for(int i=0;i<512;++i){float v=.25f*std::sin(juce::MathConstants<double>::twoPi*440*(blk*512+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);if(blk>200)for(int i=0;i<512;++i)steady.push_back(b.getSample(0,i));}
  int bestLag=0;double best=-1;for(int lag=90;lag<115;++lag){double cross=0;for(size_t i=0;i+lag<steady.size();++i)cross+=steady[i]*steady[i+lag];if(cross>best){best=cross;bestLag=lag;}}
  if(bestLag<=0)return 104;const double f=48000.0/bestLag;if(std::abs(f-493.88)>5){std::cerr<<"Amount-zero Shift output "<<f<<" Hz\n";return 104;}}
 // Signalsmith bypass remains aligned at 768 kHz (120 ms = 92160 samples).
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID=="smith")p->setValueNotifyingHost(1.f);
  r->setRateAndBufferSizeDetails(768000,512);r->prepareToPlay(768000,512);const int latency=r->getLatencySamples();
  juce::AudioBuffer<float> b(2,512);juce::MidiBuffer mb;int onset=-1;
  for(int blk=0;blk<200&&onset<0;++blk){b.clear();if(blk==0){b.setSample(0,0,1.f);b.setSample(1,0,1.f);}r->processBlockBypassed(b,mb);
   for(int i=0;i<512;++i)if(std::abs(b.getSample(0,i))>1.0e-4f){onset=blk*512+i;break;}}
  if(onset!=latency||latency<90000){std::cerr<<"768k bypass onset "<<onset<<" latency "<<latency<<"\n";return 105;}}
 // Every parameter must survive a state save/restore, not just the audio controls.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());auto& params=r->getParameters();
  auto validValue=[&](size_t i,bool second){if(dynamic_cast<juce::AudioParameterBool*>(params[(int)i])!=nullptr)return second?1.f:0.f;
   if(auto* choice=dynamic_cast<juce::AudioParameterChoice*>(params[(int)i])){const int count=juce::jmax(1,choice->choices.size());return (float)((second?count-1:i)%count)/(float)juce::jmax(1,count-1);}
   return second?.8f:.2f;};
  std::vector<float> expected;
  for(size_t i=0;i<params.size();++i){params[(int)i]->setValueNotifyingHost(validValue(i,false));expected.push_back(params[(int)i]->getValue());}
  juce::MemoryBlock saved;r->getStateInformation(saved);
  for(size_t i=0;i<params.size();++i)params[(int)i]->setValueNotifyingHost(validValue(i,true));
  r->setStateInformation(saved.getData(),(int)saved.getSize());
  for(size_t i=0;i<params.size();++i)if(std::abs(params[(int)i]->getValue()-expected[i])>.001f){std::cerr<<"state restore failed for "<<params[(int)i]->getName(64)<<" expected "<<expected[i]<<" got "<<params[(int)i]->getValue()<<"\n";return 107;}}
 // Long-session parameter and engine churn must remain finite.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* p:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))if(w->paramID==id){p->setValueNotifyingHost(v);return;}};
  for(int n=0;n<12;++n)setP(("note"+juce::String(n)).toRawUTF8(),1.f);setP("key",0.f);
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::AudioBuffer<float> b(2,256);juce::MidiBuffer mb;double phase=0;
  for(int blk=0;blk<4000;++blk){
   if(blk%200==0){const int step=(blk/200)%6;setP("lowlatency",step==1?1.f:0.f);setP("engine",step==2?1.f:0.f);setP("smith",step==3?1.f:0.f);setP("bypass",step==4?1.f:0.f);
    setP("speed",step==5?-3.f:25.f);setP("amount",step==5?0.f:1.f);setP("doubler",step==2?.65f:0.f);}
   for(int i=0;i<256;++i){const float v=.2f*std::sin(phase);phase+=juce::MathConstants<double>::twoPi*220/48000;b.setSample(0,i,v);b.setSample(1,i,v);}
   r->processBlock(b,mb);for(int c=0;c<2;++c)for(int i=0;i<256;++i)if(!std::isfinite(b.getSample(c,i))){std::cerr<<"long-session non-finite at block "<<blk<<"\n";return 106;}}}
juce::ScopedJuceInitialiser_GUI init;std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());auto* rangedSpeed=dynamic_cast<juce::RangedAudioParameter*>(p->getParameters()[1]);
 auto* rangedAmount=dynamic_cast<juce::RangedAudioParameter*>(p->getParameters()[3]);
 if(!rangedSpeed || !rangedAmount || std::abs(rangedSpeed->convertFrom0to1(0.f)-200.f)>.001f || std::abs(rangedSpeed->convertFrom0to1(.392f)-25.f)>.001f || std::abs(rangedSpeed->convertFrom0to1(.784f))>.001f || std::abs(rangedSpeed->convertFrom0to1(1.f)+3.f)>.001f || std::abs(rangedAmount->convertFrom0to1(.5f)-75.f)>.001f) {std::cerr<<"Recovered parameter landmarks differ\n";return 13;}
 for(int i=0;i<=100;++i) {float n=i/100.f;if(std::abs(rangedSpeed->convertTo0to1(rangedSpeed->convertFrom0to1(n))-n)>.0001f || std::abs(rangedAmount->convertTo0to1(rangedAmount->convertFrom0to1(n))-n)>.0001f)return 14;}
 if(p->getNumPrograms()<8){std::cerr<<"Factory presets missing\n";return 17;}
 bool hasDoubler=false;for(auto* v:p->getParameters())if(v->getName(64)=="Doubler")hasDoubler=true;if(!hasDoubler)return 18;
 for(int program=0;program<p->getNumPrograms();++program){p->setCurrentProgram(program);if(p->getCurrentProgram()!=program||p->getProgramName(program).isEmpty())return 19;}
 if(p->getNumPrograms()<16){std::cerr<<"Industry preset set missing\n";return 48;}
 {auto* sp=dynamic_cast<juce::RangedAudioParameter*>(p->getParameters()[1]);
  p->setCurrentProgram(7);if(sp==nullptr||sp->convertFrom0to1(sp->getValue())>=0.f){std::cerr<<"Trap Robotic must use negative Speed\n";return 49;}
  p->setCurrentProgram(6);float doubler=-1;for(auto* v:p->getParameters())if(v->getName(64)=="Doubler")doubler=v->getValue();
  if(doubler<.6f){std::cerr<<"Doubler Wide preset is not wet\n";return 50;}
  p->setCurrentProgram(0);}
 // Presets change the sound; the song's key and scale stay put unless key lock is off.
 {std::unique_ptr<juce::AudioProcessor> q(createPluginFilter());
  auto ranged=[&](const char* id)->juce::RangedAudioParameter*{for(auto* v:q->getParameters())if(auto* w=dynamic_cast<juce::RangedAudioParameter*>(v))if(w->paramID==id)return w;return nullptr;};
  auto* keyParam=ranged("key");auto* speedParam=ranged("speed");if(!keyParam||!speedParam)return 119;
  auto keyNow=[&]{return (int)std::lround(keyParam->convertFrom0to1(keyParam->getValue()));};
  auto setPlain=[](juce::RangedAudioParameter* v,float plain){v->setValueNotifyingHost(v->convertTo0to1(plain));};
  setPlain(keyParam,7.f);q->setCurrentProgram(4); // R&B Smooth is stored in A minor
  if(keyNow()!=7){std::cerr<<"Preset changed the key while key lock is on\n";return 120;}
  megatune::setKeepKey(*q,false);q->setCurrentProgram(4);
  if(keyNow()!=9){std::cerr<<"Preset did not recall its key with key lock off\n";return 121;}
  megatune::setKeepKey(*q,true);
  const juce::String probe("megatune selftest preset");
  setPlain(speedParam,137.f);setPlain(keyParam,2.f);
  if(!megatune::saveUserPreset(*q,probe)){std::cerr<<"User preset did not save\n";return 122;}
  q->setCurrentProgram(7);setPlain(keyParam,5.f);
  if(!megatune::loadUserPreset(*q,probe)){std::cerr<<"User preset did not load\n";megatune::deleteUserPreset(*q,probe);return 123;}
  const float speedBack=speedParam->convertFrom0to1(speedParam->getValue());const int keyBack=keyNow();
  const bool removed=megatune::deleteUserPreset(*q,probe);
  if(std::abs(speedBack-137.f)>.5f){std::cerr<<"User preset lost Speed: "<<speedBack<<"\n";return 124;}
  if(keyBack!=5){std::cerr<<"User preset changed the key while key lock is on\n";return 125;}
  if(!removed||megatune::loadUserPreset(*q,probe)){std::cerr<<"User preset was not deleted\n";return 126;}}
 if(!(megaTuneSmoothingSeconds(-3,0,0)<megaTuneSmoothingSeconds(25,0,0))){std::cerr<<"Negative Speed must tighten faster than the default\n";return 51;}
 {std::unique_ptr<juce::AudioProcessor> q(createPluginFilter());juce::MidiBuffer ev;juce::AudioBuffer<float> b(2,256);
  for(int program=0;program<q->getNumPrograms();++program){q->setCurrentProgram(program);q->prepareToPlay(48000,256);
   for(int blk=0;blk<6;++blk){for(int i=0;i<256;++i)for(int c=0;c<2;++c)b.setSample(c,i,.25f*std::sin(juce::MathConstants<double>::twoPi*220*(blk*256+i)/48000));q->processBlock(b,ev);
    for(int c=0;c<2;++c)for(int i=0;i<256;++i)if(!std::isfinite(b.getSample(c,i))){std::cerr<<"Preset "<<program<<" produced non-finite audio\n";return 52;}}}}

 p->setCurrentProgram(0);
 for(auto* parameter:p->getParameters())if(parameter->getName(64)=="Width" && (parameter->getDefaultValue()!=0.f || parameter->getValue()!=0.f)){std::cerr<<"Original Width default must be zero\n";return 23;}

 // DAW sessions must restore the selected factory program as well as its sound.
 {std::unique_ptr<juce::AudioProcessor> source(createPluginFilter());source->setCurrentProgram(6);
 juce::MemoryBlock session;source->getStateInformation(session);
 std::unique_ptr<juce::AudioProcessor> reopened(createPluginFilter());
 reopened->setStateInformation(session.getData(),(int)session.getSize());
 if(reopened->getCurrentProgram()!=6){std::cerr<<"Factory program lost on session reopen\n";return 24;}
 std::unique_ptr<juce::AudioProcessorEditor> view(reopened->createEditorAndMakeActive());
 auto* presetCombo=dynamic_cast<juce::ComboBox*>(findById(view.get(),"preset-combo"));
 const bool presetCorrect=presetCombo!=nullptr&&presetCombo->getNumItems()==reopened->getNumPrograms()&&presetCombo->getSelectedId()==7;
 if(!presetCorrect){std::cerr<<"Preset menu disagrees with host program\n";return 25;}
 // Exercise each visible switch and scale action through its actual UI callback.
 auto parameter=[&](const juce::String& id)->juce::AudioProcessorParameterWithID*{
 for(auto* value:reopened->getParameters())if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(value))if(identified->paramID==id)return identified;return nullptr;};
 visitChildren(view.get(),[&](juce::Component* child){if(auto* combo=dynamic_cast<juce::ComboBox*>(child))if(combo->getNumItems()==12)combo->setSelectedId(3,juce::sendNotificationSync);});
 if(std::abs(parameter("key")->getValue()-2.f/11.f)>.001f)return 26;
 visitChildren(view.get(),[&](juce::Component* child){if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()=="Chromatic"&&button->onClick)button->onClick();});
 for(int i=0;i<12;++i)if(parameter("note"+juce::String(i))->getValue()!=1)return 27;
 visitChildren(view.get(),[&](juce::Component* child){if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()=="C")button->setToggleState(false,juce::sendNotificationSync);});
 if(parameter("note10")->getValue()!=0 || parameter("note0")->getValue()!=1){std::cerr<<"Key change left stale keyboard wiring\n";return 28;}
 int uiFailure=0;
 visitChildren(view.get(),[&](juce::Component* child){if(auto* toggle=dynamic_cast<juce::ToggleButton*>(child)){
 toggle->setToggleState(true,juce::sendNotificationSync);
 if(toggle->getButtonText()=="Global Bypass"&&parameter("bypass")->getValue()!=1)uiFailure=29;
 if(toggle->getButtonText()=="Note Stabilizer"){toggle->setToggleState(false,juce::sendNotificationSync);if(parameter("stabilize")->getValue()!=0)uiFailure=30;}}});
 if(uiFailure)return uiFailure;
 visitChildren(view.get(),[&](juce::Component* child){if(auto* slider=dynamic_cast<juce::Slider*>(child)){
 slider->setValue(slider->getMaximum(),juce::sendNotificationSync);
 const auto id=slider->getName().toLowerCase();auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter(id));if(ranged==nullptr||std::abs(ranged->convertFrom0to1(ranged->getValue())-slider->getMaximum())>.01f)uiFailure=31;}});
 if(uiFailure)return uiFailure;

 }
 // Toolbar actions must affect the actual host parameters, including A/B recall.
 {std::unique_ptr<juce::AudioProcessor> host(createPluginFilter());
 std::unique_ptr<juce::AudioProcessorEditor> view(host->createEditorAndMakeActive());
 auto click=[&](const juce::String& label){bool did=false;visitChildren(view.get(),[&](juce::Component* child){if(!did)if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==label&&button->onClick){button->onClick();did=true;}});return did;};
 if(!click(">")||host->getCurrentProgram()!=1){std::cerr<<"Next preset button missing or inactive\n";return 32;}
 if(!click("<")||host->getCurrentProgram()!=0)return 33;
 if(!click("Undo")||host->getCurrentProgram()!=1)return 34;
 if(!click("Redo")||host->getCurrentProgram()!=0)return 35;
 click(">");if(!click("A > B"))return 36;click(">");if(host->getCurrentProgram()!=2)return 37;
 if(!click("B")||host->getCurrentProgram()!=1){std::cerr<<"A/B did not recall saved settings\n";return 38;}
 if(!click("A")||host->getCurrentProgram()!=2)return 39;
 click("Undo");if(host->getCurrentProgram()!=2){std::cerr<<"Comparison navigation leaked into edit history\n";return 40;}
 }
 // A delayed second voice must create stereo differences with a mono source.
 {std::unique_ptr<juce::AudioProcessor> fx(createPluginFilter());fx->setCurrentProgram(6);fx->prepareToPlay(48000,256);juce::MidiBuffer events;juce::AudioBuffer<float> samples(2,256);double difference=0;for(int block=0;block<50;++block){for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch)samples.setSample(ch,i,.3f*std::sin((block*256+i)*.057));fx->processBlock(samples,events);if(block>20)for(int i=0;i<256;++i){if(!std::isfinite(samples.getSample(0,i)))return 20;difference+=std::abs(samples.getSample(0,i)-samples.getSample(1,i));}}if(difference<1)return 21;
 juce::MemoryBlock saved;fx->getStateInformation(saved);std::unique_ptr<juce::AudioProcessor> copy(createPluginFilter());copy->setStateInformation(saved.getData(),(int)saved.getSize());for(auto* parameter:copy->getParameters())if(parameter->getName(64)=="Doubler"&&std::abs(parameter->getValue()-.65f)>.001f)return 22;}
 // Recovered Reference and Low Latency controls must reach the DSP.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto byId=[&](const juce::String& id)->juce::RangedAudioParameter*{for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v))if(w->paramID==id)return dynamic_cast<juce::RangedAudioParameter*>(v);return nullptr;};
  auto* ref=byId("reference");auto* low=byId("lowlatency");
  if(ref==nullptr||low==nullptr){std::cerr<<"Reference or Low Latency parameter missing\n";return 41;}
  if(std::abs(ref->convertFrom0to1(ref->getDefaultValue())-440.f)>.01f)return 42;
  r->prepareToPlay(48000,256);if(r->getLatencySamples()!=2048)return 43;
  low->setValueNotifyingHost(1.f);r->prepareToPlay(48000,256);if(r->getLatencySamples()!=512)return 44;
  low->setValueNotifyingHost(0.f);r->prepareToPlay(48000,256);if(r->getLatencySamples()!=2048)return 45;
 }
 // With A4=432 the same 432 Hz input must pass through uncorrected.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){if(w->paramID=="reference")if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(v))ranged->setValueNotifyingHost(ranged->convertTo0to1(432.f));if(w->paramID.startsWith("note"))v->setValueNotifyingHost(1.f);}
  r->prepareToPlay(48000,256);juce::MidiBuffer midi;juce::AudioBuffer<float> buf(2,256);std::vector<float> steady;
  for(int b=0;b<220;++b){for(int i=0;i<256;++i)for(int c=0;c<2;++c)buf.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(b*256+i)/48000));r->processBlock(buf,midi);if(b>120)for(int i=0;i<256;++i)steady.push_back(buf.getSample(0,i));}
  int bestLag=0;double best=-1;for(int lag=100;lag<125;++lag){double cross=0;for(size_t i=0;i+lag<steady.size();++i)cross+=steady[i]*steady[i+lag];if(cross>best){best=cross;bestLag=lag;}}
  if(bestLag<=0)return 46;double f=48000.0/bestLag;std::cout<<"Reference 432 Hz output: "<<f<<" Hz\n";if(std::abs(f-432)>3)return 47;
 }
 // Negative Shift must transpose the corrected signal down.
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
   if(w->paramID=="shift")if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(v))ranged->setValueNotifyingHost(ranged->convertTo0to1(-1.f));
   if(w->paramID.startsWith("note"))v->setValueNotifyingHost(1.f);}
  r->prepareToPlay(48000,256);juce::MidiBuffer midi;juce::AudioBuffer<float> buf(2,256);std::vector<float> steady;
  for(int b=0;b<220;++b){for(int i=0;i<256;++i)for(int c=0;c<2;++c)buf.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(b*256+i)/48000));r->processBlock(buf,midi);if(b>120)for(int i=0;i<256;++i)steady.push_back(buf.getSample(0,i));}
  int bestLag=0;double best=-1;for(int lag=100;lag<125;++lag){double cross=0;for(size_t i=0;i+lag<steady.size();++i)cross+=steady[i]*steady[i+lag];if(cross>best){best=cross;bestLag=lag;}}
  if(bestLag<=0)return 53;double f=48000.0/bestLag;std::cout<<"Shift -1 output: "<<f<<" Hz\n";if(std::abs(f-415.3)>4)return 54;
 }
 // Validation on a vowel-like signal with vibrato: tuning accuracy.
 {const double sr=48000;const int n=sr*2;std::vector<float> in((size_t)n);
  double phase=0;for(int i=0;i<n;++i){const double t=i/sr;const double f0=210.0*std::pow(2.0,35.0*std::sin(2*juce::MathConstants<double>::pi*5.5*t)/1200.0);
   phase+=2*juce::MathConstants<double>::pi*f0/sr;double v=0;
   for(int k=1;k<=24;++k){const double fk=f0*k;if(fk>8000)break;
    const double a=1.0/(1.0+std::pow((fk-700.0)/130.0,2))+0.7/(1.0+std::pow((fk-1200.0)/180.0,2));
    v+=a*std::sin(phase*k);}
   in[(size_t)i]=(float)(0.12*v);}
  std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
   if(w->paramID=="key")v->setValueNotifyingHost(0.f);
   if(w->paramID.startsWith("note"))v->setValueNotifyingHost(w->paramID=="note9"?1.f:0.f);
   if(w->paramID=="speed")if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(v))ranged->setValueNotifyingHost(ranged->convertTo0to1(40.f));}
  r->prepareToPlay(sr,256);juce::MidiBuffer midi;juce::AudioBuffer<float> buf(2,256);std::vector<float> out((size_t)n,0.f);
  for(int b=0;b*256<n;++b){for(int i=0;i<256;++i){const int idx=b*256+i;buf.setSample(0,i,in[(size_t)idx]);buf.setSample(1,i,in[(size_t)idx]);}
   r->processBlock(buf,midi);for(int i=0;i<256;++i)if(b*256+i<n)out[(size_t)(b*256+i)]=buf.getSample(0,i);}
  double err=0;int count=0;for(int w=8192;w+2048<n;w+=2048){int bestLag=0;double best=-1;
   for(int lag=200;lag<240;++lag){double c=0;for(int i=0;i<1600;++i)c+=out[(size_t)(w+i)]*out[(size_t)(w+i+lag)];if(c>best){best=c;bestLag=lag;}}
   if(bestLag<=0)continue;const double f=sr/bestLag;err+=std::abs(1200.0*std::log2(f/220.0));++count;}
  err/=count;
  std::cout<<"Vowel: mean tuning error "<<err<<" cents (210 Hz +-35 cents vibrato -> A3 220 Hz)\n";
  if(err>25)return 58;
 }
 // Range limiting must leave notes outside the interval uncorrected.
 {auto run=[&](float semis)->double{std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
    if(w->paramID=="key")v->setValueNotifyingHost(0.f);
    if(w->paramID.startsWith("note"))v->setValueNotifyingHost(w->paramID=="note9"?1.f:0.f);
    if(w->paramID=="range")if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(v))ranged->setValueNotifyingHost(ranged->convertTo0to1(semis));}
   r->prepareToPlay(48000,256);juce::MidiBuffer midi;juce::AudioBuffer<float> b(2,256);std::vector<float> steady;
   for(int blk=0;blk<300;++blk){for(int i=0;i<256;++i)for(int c=0;c<2;++c)b.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*200*(blk*256+i)/48000));r->processBlock(b,midi);if(blk>200)for(int i=0;i<256;++i)steady.push_back(b.getSample(0,i));}
   int bestLag=0;double best=-1;for(int lag=190;lag<260;++lag){double c=0;for(size_t i=0;i+lag<steady.size();++i)c+=steady[i]*steady[i+lag];if(c>best){best=c;bestLag=lag;}}
   return bestLag>0?48000.0/bestLag:0;};
  const double limited=run(1.f),full=run(3.f);
  std::cout<<"Range: 1 st -> "<<limited<<" Hz, 3 st -> "<<full<<" Hz (200 Hz in, target A3 220)\n";
  if(!(std::abs(limited-200)<6))return 59;
  if(!(std::abs(full-220)<6))return 60;
 }


 // Quality and low-latency modes must both tune; quality must be smoother.
 {auto run=[&](bool lowLatency,double&freqOut,double&ripple)->bool{
   std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
     if(w->paramID=="lowlatency")v->setValueNotifyingHost(lowLatency?1.f:0.f);
     if(w->paramID.startsWith("note"))v->setValueNotifyingHost(1.f);}
   r->prepareToPlay(48000,256);
   juce::MidiBuffer midi;juce::AudioBuffer<float> buf(2,256);std::vector<float> steady;
   for(int b=0;b<400;++b){for(int i=0;i<256;++i)for(int c=0;c<2;++c)buf.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(b*256+i)/48000));r->processBlock(buf,midi);if(b>300)for(int i=0;i<256;++i)steady.push_back(buf.getSample(0,i));}
   int bestLag=0;double best=-1;for(int lag=100;lag<125;++lag){double cross=0;for(size_t i=0;i+lag<steady.size();++i)cross+=steady[i]*steady[i+lag];if(cross>best){best=cross;bestLag=lag;}}
   if(bestLag<=0)return false;freqOut=48000.0/bestLag;
   double sum=0,sum2=0;int n=0;for(size_t i=0;i+64<steady.size();i+=64){double e=0;for(int k=0;k<64;++k)e+=std::abs(steady[i+k]);e/=64;sum+=e;sum2+=e*e;++n;}
   const double mean=sum/n;ripple=std::sqrt(std::max(0.0,sum2/n-mean*mean))/(mean>1e-9?mean:1);return true;};
  double qf=0,qr=0,lf=0,lr=0;
  if(!run(false,qf,qr)||!run(true,lf,lr))return 55;
  std::cout<<"Quality "<<qf<<" Hz ripple "<<qr<<", low latency "<<lf<<" Hz ripple "<<lr<<std::endl;
  if(std::abs(qf-440)>4||std::abs(lf-440)>4)return 56;
  if(!(qr<0.12&&lr<0.12)){std::cerr<<"Both modes must be smooth with the PSOLA engine (qr="<<qr<<" lr="<<lr<<")\n";return 57;}
 }



 
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto pid=[&](const juce::String& id)->juce::AudioProcessorParameter*{for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v))if(w->paramID==id)return v;return nullptr;};
  for(int n=0;n<12;++n)if(auto* q=pid("note"+juce::String(n)))q->setValueNotifyingHost((n==8||n==9)?1.f:0.f);
  if(auto* q=pid("key"))q->setValueNotifyingHost(0.f);
  if(auto* q=pid("shift"))q->setValueNotifyingHost(0.5f);
  if(auto* q=pid("range"))q->setValueNotifyingHost(1.f);
  if(auto* q=pid("amount"))q->setValueNotifyingHost(1.f);
  if(auto* q=pid("speed"))q->setValueNotifyingHost(1.f);
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  auto feed=[&]{juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);std::vector<float> st;
    for(int blk=0;blk<260;++blk){for(int i=0;i<256;++i)for(int c=0;c<2;++c)b.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i)/48000));r->processBlock(b,mb);
      if(blk>160)for(int i=0;i<256;++i)st.push_back(b.getSample(0,i));}
    int best=0;double bs=-1;for(int lag=90;lag<120;++lag){double c=0;for(size_t i=0;i+lag<st.size();++i)c+=st[i]*st[i+lag];if(c>bs){bs=c;best=lag;}}
    return best>0?48000.0/best:0.0;};
  const double f0a=feed();
  auto* ed=r->createEditorIfNeeded();
  auto click=[&](const juce::String& txt)->bool{bool did=false;visitChildren(ed,[&](juce::Component* child){if(!did)if(auto* bt=dynamic_cast<juce::TextButton*>(child))if(bt->getButtonText().startsWith(txt)&&bt->onClick){bt->onClick();did=true;}});return did;};
  click("Why this note?");
  std::function<juce::Component*(juce::Component*,const juce::String&)> find=[&](juce::Component* root,const juce::String& id)->juce::Component*{if(root->getComponentID()==id)return root;for(int i=0;i<root->getNumChildComponents();++i)if(auto* c=find(root->getChildComponent(i),id))return c;return nullptr;};
  auto* panel=find(ed,"history-panel");
  auto* row=panel?find(panel,"history-row-0"):nullptr;
  auto* ex=panel?find(panel,"history-exclude"):nullptr;
  if(row)dynamic_cast<juce::Button*>(row)->onClick();
  const bool excluded=ex&&ex->isEnabled();if(ex)dynamic_cast<juce::Button*>(ex)->onClick();
  const double f0b=feed();
  const bool undone=click("Undo");const double f0c=feed();
  std::cout<<"WHY excluded="<<excluded<<" F0="<<f0a<<" -> "<<f0b<<" undo "<<f0c<<"\n";
  if(!((std::abs(f0a-440)<4)&&(std::abs(f0b-415.3)<5)&&(std::abs(f0c-440)<4)&&excluded&&undone)){std::cerr<<"WHY interaction failed\n";return 60;}
  r->editorBeingDeleted(ed);delete ed;
 }

 // ---- target-change demo: scan the whole take for a sustained target ----
 {juce::AudioFormatManager fm;fm.registerBasicFormats();
  juce::File dry(juce::File::getCurrentWorkingDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("test-audio/ade-dry.wav"));
  std::unique_ptr<juce::AudioFormatReader> rd(dry.exists()?fm.createReaderFor(dry):nullptr);
  if(rd==nullptr){std::cerr<<"VOCAL fixture missing; skipping\n";}
  else{
  const int sr2=(int)rd->sampleRate;const int total=(int)juce::jmin((juce::int64)rd->lengthInSamples,(juce::int64)sr2*40);
  juce::AudioBuffer<float> all(1,total);rd->read(&all,0,total,0,true,false);
  auto mk=[&](bool gOn,int mutePc){std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
     if(w->paramID.startsWith("note")){const int idx=w->paramID.getTrailingIntValue();v->setValueNotifyingHost((idx==mutePc&&!gOn)?0.f:1.f);}
     if(w->paramID=="key")v->setValueNotifyingHost(0.f);
     if(w->paramID=="shift")v->setValueNotifyingHost(0.5f);
     if(w->paramID=="range")v->setValueNotifyingHost(1.f);
     if(w->paramID=="amount")v->setValueNotifyingHost(1.f);}
   r->setRateAndBufferSizeDetails(sr2,256);r->prepareToPlay(sr2,256);return r;};
  auto scan=mk(true,-1);std::vector<int> trace;
  {juce::MidiBuffer mb;juce::AudioBuffer<float> b(1,256);
   for(int pos=0;pos<total;pos+=256){const int m=juce::jmin(256,total-pos);b.clear();for(int i2=0;i2<m;++i2)b.setSample(0,i2,all.getSample(0,pos+i2));scan->processBlock(b,mb);trace.push_back(megatune::lastTargetPc.load());}}
  int bestStart=-1,bestLen=0,bestPc=-1;
  for(size_t k=0;k<trace.size();){const int pc=trace[k];if(pc<0){++k;continue;}size_t e=k;while(e<trace.size()&&trace[e]==pc)++e;
   const int len=(int)(e-k);const bool better=(bestStart<0)||(pc==7&&bestPc!=7)||(pc==bestPc&&len>bestLen);
   if(better){bestStart=(int)k;bestLen=len;bestPc=pc;}k=e;}
  if(bestStart<0){std::cout<<"VOCAL no sustained target; synthetic demo stands\n";}
  else{
   const int wStart=bestStart*256;const int wLen=juce::jmin(bestLen*256,(int)(1.2*sr2));
   auto run=[&](bool gOn,int mutePc,std::vector<int>&tr,std::vector<float>&au){auto r=mk(gOn,mutePc);{float nv=-1;for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v))if(w->paramID=="note11")nv=v->getValue();std::cerr<<"RUN gOn="<<gOn<<" mutePc="<<mutePc<<" note11="<<nv<<"\n";}juce::MidiBuffer mb;juce::AudioBuffer<float> b(1,256);
    for(int pos=0;pos<wStart+wLen;pos+=256){const int m=juce::jmin(256,total-pos);b.clear();for(int i2=0;i2<m;++i2)b.setSample(0,i2,all.getSample(0,pos+i2));
     r->processBlock(b,mb);if(pos>=wStart){tr.push_back(megatune::lastTargetPc.load());for(int i2=0;i2<m;++i2)au.push_back(b.getSample(0,i2));}}
    std::cerr<<"RUNEND gOn="<<(int)gOn<<" mask="<<(int)megatune::debugMask.load()<<" pc11="<<((megatune::debugMask.load()>>11)&1)<<" nearest="<<megatune::debugNearest.load()<<" key="<<megatune::debugKey.load()<<"\n";};
   std::vector<int> tb,te,tu;std::vector<float> ab,ae,au;run(true,bestPc,tb,ab);run(false,bestPc,te,ae);run(true,bestPc,tu,au);
   int d1=0,d2=0;for(size_t k=0;k<tb.size()&&k<te.size();++k)if(tb[k]!=te[k])++d1;
   for(size_t k=0;k<tb.size()&&k<tu.size();++k)if(tb[k]!=tu[k])++d2;
   auto dump=[&](const std::vector<float>&src,const juce::String&tag){juce::File f=juce::File::getCurrentWorkingDirectory().getChildFile("whyclip_"+tag+".wav");f.deleteFile();
    juce::WavAudioFormat w;juce::AudioBuffer<float> o(1,(int)src.size());for(size_t i2=0;i2<src.size();++i2)o.setSample(0,(int)i2,src[i2]);
    std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(new juce::FileOutputStream(f),sr2,1,24,{},0));if(wr)wr->writeFromAudioSampleBuffer(o,0,o.getNumSamples());};
   dump(ab,"before");dump(ae,"excluded");dump(au,"undo");
   static const char* ns[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
   std::cerr<<"DBG mask="<<(int)megatune::debugMask.load()<<" nearest="<<megatune::debugNearest.load()<<" key="<<megatune::debugKey.load()<<" midi="<<(megatune::debugMidi.load()/100.0)<<" pc11enabled="<<((megatune::debugMask.load()>>11)&1)<<"\n";
   std::cout<<"VOCAL window="<<juce::String(wStart/(double)sr2,2)<<"s len="<<juce::String(wLen/(double)sr2,2)
            <<"s target="<<ns[bestPc]<<" blocks_changed="<<d1<<"/"<<tb.size()<<" undo_diff="<<d2<<"\n";
   {int nb=0,ne=0;for(int x:tb)if(x>=0)++nb;for(int x:te)if(x>=0)++ne;
    std::cerr<<"TRACE voiced before="<<nb<<" excluded="<<ne<<" of "<<tb.size()<<" first_before="<<(tb.empty()?-9:tb[0])<<" first_excluded="<<(te.empty()?-9:te[0])<<"\n";
    for(size_t k=0;k<tb.size()&&k<te.size();++k)if(tb[k]!=te[k]){std::cerr<<"first diff at block "<<k<<" before="<<tb[k]<<" excluded="<<te[k]<<"\n";break;}}
   if(d1==0||d2!=0)std::cerr<<"VOCAL demo inconclusive (does not abort)\n";
  }}}

 // ---- deterministic disabled-target check: 490 Hz, A#/B/C enabled ----
 {auto mk2=[&](bool bOn,bool stab){std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(v)){
     if(w->paramID.startsWith("note")){const int idx=w->paramID.getTrailingIntValue();const bool on=(idx==10||idx==0||(idx==11&&bOn));v->setValueNotifyingHost(on?1.f:0.f);}
     if(w->paramID=="key")v->setValueNotifyingHost(0.f);
     if(w->paramID=="shift")v->setValueNotifyingHost(0.5f);
     if(w->paramID=="range")v->setValueNotifyingHost(1.f);
     if(w->paramID=="amount")v->setValueNotifyingHost(1.f);
     if(w->paramID=="speed")v->setValueNotifyingHost(0.f);
     if(w->paramID=="stabilize")v->setValueNotifyingHost(stab?1.f:0.f);}
   r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);return r;};
  auto run2=[&](bool bOn,bool stab){auto r=mk2(bOn,stab);juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);std::vector<float> st;
   for(int blk=0;blk<260;++blk){for(int i=0;i<256;++i)for(int c=0;c<2;++c)b.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*490*(blk*256+i)/48000));r->processBlock(b,mb);
     if(blk>160)for(int i=0;i<256;++i)st.push_back(b.getSample(0,i));}
   int best=0;double bs=-1;for(int lag=80;lag<120;++lag){double c=0;for(size_t i=0;i+lag<st.size();++i)c+=st[i]*st[i+lag];if(c>bs){bs=c;best=lag;}}
   return best>0?48000.0/best:0.0;};
  const double v1=run2(true,false),v2=run2(false,false),v3=run2(false,true);
  std::cout<<"DET B-on="<<v1<<" B-off="<<v2<<" B-off+stab="<<v3<<" (expect ~493.9 / 466.2 / 466.2)\n";
 }

 // ---- engine switching on a live instance: declared latency transitions ----
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const juce::String& id,float v){for(auto* q:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(q))if(w->paramID==id){q->setValueNotifyingHost(v);return;}};
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  auto flush=[&]{juce::AudioBuffer<float> b(2,512);b.clear();juce::MidiBuffer mb;r->processBlock(b,mb);};
  auto step=[&](float smith,float psola,float low)->int{setP("smith",smith);setP("engine",psola);setP("lowlatency",low);flush();r->prepareToPlay(48000,256);return r->getLatencySamples();};
  const int q0=step(0,0,0), s1=step(1,0,0), p2=step(0,1,0), l3=step(0,0,1), q4=step(0,0,0);
  std::cout<<"SWITCH quality="<<q0<<" smith="<<s1<<" psola="<<p2<<" low="<<l3<<" back="<<q4<<"\n";
  if(!(q0==2048&&s1==5760&&p2==2048&&l3==512&&q4==2048)){std::cerr<<"SWITCH latency transition wrong\n";return 63;}
 }

 // identical stereo input must yield identical channels when the doubler is off
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* q:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(q))if(w->paramID==id){q->setValueNotifyingHost(v);return;}};
  setP("doubler",0.f);setP("width",0.f);setP("amount",1.f);setP("shift",0.5f);
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);double worst=0;
  for(int blk=0;blk<200;++blk){for(int i=0;i<256;++i){const float v=.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}
   r->processBlock(b,mb);for(int i=0;i<256;++i)worst=juce::jmax(worst,(double)std::abs(b.getSample(0,i)-b.getSample(1,i)));}
  std::cout<<"STEREO max L/R difference="<<worst<<"\n";
  if(worst>1e-6){std::cerr<<"stereo channels diverged\n";return 64;}
 }

 // oversized block with Signalsmith active must not allocate and must stay finite
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  auto setP=[&](const char* id,float v){for(auto* q:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(q))if(w->paramID==id){q->setValueNotifyingHost(v);return;}};
  setP("smith",1.f);r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  juce::MidiBuffer mb;juce::AudioBuffer<float> big(2,10000);for(int i=0;i<10000;++i){const float v=.2f*std::sin(juce::MathConstants<double>::twoPi*440*i/48000);big.setSample(0,i,v);big.setSample(1,i,v);}
  r->processBlock(big,mb);bool finite=true;for(int c=0;c<2;++c)for(int i=0;i<10000;++i)if(!std::isfinite(big.getSample(c,i)))finite=false;
  std::cout<<"BIGBLOCK finite="<<(finite?"yes":"no")<<"\n";
  if(!finite){std::cerr<<"oversized smith block produced non-finite audio\n";return 66;}
 }

 // Signalsmith must shift pitch whether or not Smith Comp was stored as on
 {for(float comp: {0.f,1.f}){
   std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   auto setP=[&](const char* id,float v){for(auto* q:r->getParameters())if(auto* w=dynamic_cast<juce::AudioProcessorParameterWithID*>(q))if(w->paramID==id){q->setValueNotifyingHost(v);return;}};
   setP("smith",1.f);setP("smithcomp",comp);setP("shift",(3.f+12.f)/24.f);setP("amount",1.f);
   r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
   juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);std::vector<float> st;
   for(int blk=0;blk<300;++blk){for(int i=0;i<256;++i){const float v=.3f*std::sin(juce::MathConstants<double>::twoPi*220*(blk*256+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}
    r->processBlock(b,mb);if(blk>200)for(int i=0;i<256;++i)st.push_back(b.getSample(0,i));}
   int best=0;double bs=-1;for(int lag=140;lag<220;++lag){double c=0;for(size_t i=0;i+lag<st.size();++i)c+=st[i]*st[i+lag];if(c>bs){bs=c;best=lag;}}
   const double f0=48000.0/best;
   std::cout<<"SMITHCOMP comp="<<comp<<" f0="<<f0<<" (expect ~261.6)\n";
   if(std::abs(f0-261.6)>6.0){std::cerr<<"smith failed to transpose with comp="<<comp<<"\n";return 67;}
 }}

 // decision-ring readers must never observe a torn or out-of-range row
 {std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
  r->setRateAndBufferSizeDetails(48000,256);r->prepareToPlay(48000,256);
  std::atomic<bool> stop{false};std::atomic<int> bad{0},reads{0};
  std::thread reader([&]{while(!stop.load(std::memory_order_relaxed)){
    const int n=megatune::historySequence(*r);
    for(int s=n-1;s>=0&&s>=n-16;--s){const auto d=megatune::historyDecision(*r,s);reads.fetch_add(1,std::memory_order_relaxed);
      if(d.reason<0||d.reason>4||!std::isfinite(d.in)||!std::isfinite(d.target))bad.fetch_add(1,std::memory_order_relaxed);}}});
  juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);
  for(int blk=0;blk<600;++blk){for(int i=0;i<256;++i){const float v=.3f*std::sin(juce::MathConstants<double>::twoPi*220*(blk*256+i)/48000);b.setSample(0,i,v);b.setSample(1,i,v);}r->processBlock(b,mb);}
  stop.store(true);reader.join();
  std::cout<<"SEQREADS reads="<<reads.load()<<" bad="<<bad.load()<<"\n";
  if(bad.load()!=0){std::cerr<<"decision ring produced inconsistent rows\n";return 68;}
 }

 // Width on its own must widen a mono source and cancel in the mono sum.
 {auto run=[&](float width,std::vector<float>& mid,double& side){std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::RangedAudioParameter*>(v)){
    if(w->paramID=="amount")v->setValueNotifyingHost(0.f);if(w->paramID=="width")v->setValueNotifyingHost(width);}
   r->prepareToPlay(48000,256);juce::AudioBuffer<float> b(2,256);juce::MidiBuffer mb;side=0;
   for(int blk=0;blk<200;++blk){for(int i=0;i<256;++i){const double t=(blk*256+i)/48000.0;const float v=.2f*(float)(std::sin(juce::MathConstants<double>::twoPi*220*t)+.5*std::sin(juce::MathConstants<double>::twoPi*1870*t));b.setSample(0,i,v);b.setSample(1,i,v);}
    r->processBlock(b,mb);if(blk>60)for(int i=0;i<256;++i){mid.push_back(.5f*(b.getSample(0,i)+b.getSample(1,i)));side+=std::abs(b.getSample(0,i)-b.getSample(1,i));}}};
  std::vector<float> dryMid,wideMid;double drySide=0,wideSide=0;run(0.f,dryMid,drySide);run(1.f,wideMid,wideSide);
  double worst=0;for(size_t i=0;i<dryMid.size();++i)worst=std::max(worst,(double)std::abs(dryMid[i]-wideMid[i]));
  std::cout<<"WIDTH side dry="<<drySide<<" wide="<<wideSide<<" mono-sum deviation="<<worst<<"\n";
  if(drySide>1.0e-3)return 110;if(wideSide<100)return 111;if(worst>1.0e-4)return 112;}
 // Speed must reach the default engine: a slow setting leaves more vibrato than a hard one.
 {auto run=[&](float speed)->double{std::unique_ptr<juce::AudioProcessor> r(createPluginFilter());
   for(auto* v:r->getParameters())if(auto* w=dynamic_cast<juce::RangedAudioParameter*>(v)){
    if(w->paramID.startsWith("note"))v->setValueNotifyingHost(1.f);if(w->paramID=="speed")v->setValueNotifyingHost(w->convertTo0to1(speed));}
   r->prepareToPlay(48000,256);juce::AudioBuffer<float> b(2,256);juce::MidiBuffer mb;std::vector<float> out;double phase=0;
   for(int blk=0;blk<400;++blk){for(int i=0;i<256;++i){const double t=(blk*256+i)/48000.0;phase+=juce::MathConstants<double>::twoPi*222.0*std::pow(2.0,30.0*std::sin(juce::MathConstants<double>::twoPi*5.0*t)/1200.0)/48000.0;const float v=.25f*(float)(std::sin(phase)+.4*std::sin(2*phase)+.2*std::sin(3*phase));b.setSample(0,i,v);b.setSample(1,i,v);}
    r->processBlock(b,mb);if(blk>80)for(int i=0;i<256;++i)out.push_back(b.getSample(0,i));}
   double lo=1e9,hi=0;for(size_t w=0;w+2048<out.size();w+=1024){int bestLag=0;double best=-1,c[60];for(int lag=190;lag<250;++lag){double s=0;for(int i=0;i<1536;++i)s+=out[w+i]*out[w+i+lag];c[lag-190]=s;if(s>best){best=s;bestLag=lag;}}
    double frac=0;if(bestLag>190&&bestLag<249){const double l=c[bestLag-191],m=c[bestLag-190],h=c[bestLag-189];frac=.5*(l-h)/(l-2*m+h);}
    const double f=48000.0/(bestLag+frac);lo=std::min(lo,f);hi=std::max(hi,f);}
   return 1200.0*std::log2(hi/lo);};
  const double hard=run(0.f),slow=run(150.f);
  std::cout<<"SPEED residual vibrato: hard="<<hard<<" cents, slow="<<slow<<" cents (input 60)\n";
  if(!(hard<25))return 113;if(!(slow>hard+15))return 114;}

 // isolated PSOLA null test: ratio 1 must reconstruct the delayed input
 {megatune::StereoPsola ps;ps.prepare(2048,48000.0);
  const int N=48000;std::vector<float> in(N),out(N);
  for(int i=0;i<N;++i){in[i]=.3f*std::sin(2.0*3.14159265358979*220.0*i/48000.0);ps.setPeriod(48000.0f/220.0f);ps.setRatio(1.0f);const float xi[2]={in[i],in[i]};float xo[2];ps.process(xi,xo,true);out[i]=xo[0];}
  int bestLag=0;double best=-1;for(int lag=0;lag<6000;++lag){double c=0;for(int i=0;i+lag<N;i+=4)c+=out[i+lag]*in[i];if(c>best){best=c;bestLag=lag;}}
  double num=0,den=0;for(int i=0;i+bestLag<N;++i){num+=out[i+bestLag]*in[i];den+=in[i]*in[i];}
  const double g=den>0?num/den:1.0;double res=0,sig=0;
  for(int i=0;i+bestLag<N;++i){const double e=out[i+bestLag]-g*in[i];res+=e*e;sig+=in[i]*in[i];}
  const double snr=10*std::log10(sig/(res+1e-20));
  // high-frequency emphasis via first difference (crude, but sampling-rate independent)
  double hf=0,dry=0;for(int i=1;i+bestLag<N;++i){const double d=out[i+bestLag]-out[i+bestLag-1];const double r=in[i]-in[i-1];hf+=d*d;dry+=r*r;}
  const double hfdb=10*std::log10(hf/(dry+1e-20));
  std::cout<<"PSOLA_NULL lag="<<bestLag<<" gain="<<g<<" null="<<snr<<" dB hf_ratio="<<hfdb<<" dB\n";
 }
if(p->getName()!="MegaTune")return 1;for(auto* parameter:p->getParameters()){auto name=parameter->getName(64);if(name=="Speed")parameter->setValueNotifyingHost(rangedSpeed->convertTo0to1(0.f));if(name.length()<=2)parameter->setValueNotifyingHost(1);}
p->prepareToPlay(48000,256);std::vector<float> steady;juce::MidiBuffer midi;juce::AudioBuffer<float> buffer(2,256);double peak=0;for(int block=0;block<200;++block){for(int i=0;i<256;++i)for(int c=0;c<2;++c)buffer.setSample(c,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(block*256+i)/48000));p->processBlock(buffer,midi);for(int i=0;i<256;++i){float sample=buffer.getSample(0,i);if(!std::isfinite(sample)||std::abs(sample)>1)return 2;peak=juce::jmax(peak,(double)std::abs(sample));if(block>100)steady.push_back(sample);}}if(peak<.1)return 3;int bestLag=0;double bestScore=-1;for(int lag=100;lag<120;++lag){double cross=0;for(int i=0;i<16000;++i)cross+=steady[i]*steady[i+lag];if(cross>bestScore){bestScore=cross;bestLag=lag;}}double frequency=48000.0/bestLag;std::cout<<"Corrected frequency: "<<frequency<<" Hz\n";if(std::abs(frequency-440)>3)return 7;std::unique_ptr<juce::AudioProcessor> visual(createPluginFilter());std::unique_ptr<juce::AudioProcessorEditor> editor(visual->createEditorAndMakeActive());if(!editor||editor->getWidth()!=1120)return 6;
 auto pumpEditor=[&]{megatune::tickEditor(*editor);};
 auto* speedControl=findById(editor.get(),"control-speed");
 auto* experimental=findById(editor.get(),"experimental-label");
 if(!speedControl||!experimental){std::cerr<<"Editor control IDs missing\n";return 64;}
 if(experimental->isVisible()){std::cerr<<"Experimental label shown for Quality\n";return 65;}
 auto clickButton=[&](const juce::String& label){bool did=false;visitChildren(editor.get(),[&](juce::Component* child){if(!did)if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==label&&button->onClick){button->onClick();did=true;}});return did;};
 auto paramById=[&](const juce::String& id)->juce::AudioProcessorParameterWithID*{for(auto* value:visual->getParameters())if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(value))if(identified->paramID==id)return identified;return nullptr;};
 auto writeShot=[&](const juce::String& name){auto shot=editor->createComponentSnapshot(editor->getLocalBounds());juce::File f=juce::File::getCurrentWorkingDirectory().getChildFile(name);f.deleteFile();juce::FileOutputStream stream(f);juce::PNGImageFormat png2;png2.writeImageToStream(shot,stream);};
 writeShot("MegaTune-vst3-idle.png");
 {visual->setRateAndBufferSizeDetails(48000,256);visual->prepareToPlay(48000,256);juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);for(int blk=0;blk<140;++blk){for(int i2=0;i2<256;++i2)for(int c2=0;c2<2;++c2)b.setSample(c2,i2,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i2)/48000));visual->processBlock(b,mb);}}
 // A few seconds of a wobbling note, ticking the editor as a host would, so the pitch trace has something to show.
 {juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);double ph=0;long long n=0;
  for(int frame=0;frame<170;++frame){for(int blk=0;blk<6;++blk){for(int i2=0;i2<256;++i2,++n){const double t=(double)n/48000.0;
     const double hz=432.0*std::pow(2.0,(.35*std::sin(juce::MathConstants<double>::twoPi*5.2*t)+.25*std::sin(juce::MathConstants<double>::twoPi*.7*t))/12.0);
     ph+=juce::MathConstants<double>::twoPi*hz/48000.0;const float v=.3f*(float)std::sin(ph);b.setSample(0,i2,v);b.setSample(1,i2,v);}visual->processBlock(b,mb);}
   pumpEditor();}
  for(int blk=0;blk<60;++blk){for(int i2=0;i2<256;++i2)for(int c2=0;c2<2;++c2)b.setSample(c2,i2,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i2)/48000));visual->processBlock(b,mb);}}
 pumpEditor();writeShot("MegaTune-vst3-active.png");writeShot("MegaTune-vst3.png");
 const float speedBefore=paramById("speed")->getValue();
 if(!clickButton("Why this note?")){std::cerr<<"Why this note action missing\n";return 66;}
 auto* panel=findById(editor.get(),"history-panel");
 if(!panel||!panel->isVisible()){std::cerr<<"History panel did not open\n";return 67;}
 for(const char* id:{"control-speed","control-sustain","control-amount","control-doubler","control-width","control-reference","control-stabilize"})
  {auto* control=findById(editor.get(),id);if(!control||!control->isVisible()){std::cerr<<"Side-by-side control hidden: "<<id<<"\n";return 68;}}
 pumpEditor();
 auto* row0=panel->findChildWithID("history-row-0");
 auto* enableButton=panel->findChildWithID("history-enable");
 auto* excludeButton=panel->findChildWithID("history-exclude");
 auto* closeButton=panel->findChildWithID("history-close");
 if(!row0||!enableButton||!excludeButton||!closeButton){std::cerr<<"History panel controls missing\n";return 69;}
 dynamic_cast<juce::Button*>(row0)->onClick();pumpEditor();
 if(!excludeButton->isEnabled()){std::cerr<<"Exclude disabled for enabled target\n";return 70;}
 const int seqBefore=megatune::historySequence(*visual);
 const int targetBefore=megatune::historyDecision(*visual,seqBefore-1).target;
 if(targetBefore!=69){std::cerr<<"Expected A4 target, got "<<targetBefore<<"\n";return 71;}
 dynamic_cast<juce::Button*>(excludeButton)->onClick();pumpEditor();
 auto* noteA=paramById("note5");if(!noteA||noteA->getValue()>.5f){std::cerr<<"Excluding A did not clear its mask bit\n";return 72;}
 auto* undoButton=findById(editor.get(),"undo-button");if(!undoButton||!undoButton->isEnabled()){std::cerr<<"Exclude did not enter Undo history\n";return 73;}
 {juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);for(int blk=0;blk<40;++blk){for(int i2=0;i2<256;++i2)for(int c2=0;c2<2;++c2)b.setSample(c2,i2,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i2)/48000));visual->processBlock(b,mb);}}
 const int seqAfter=megatune::historySequence(*visual);
 const int targetAfter=megatune::historyDecision(*visual,seqAfter-1).target;
 if(seqAfter<=seqBefore||targetAfter==69){std::cerr<<"Next loop kept excluded target\n";return 74;}
 pumpEditor();writeShot("MegaTune-vst3-history.png");
 dynamic_cast<juce::Button*>(undoButton)->onClick();
 if(noteA->getValue()<.5f){std::cerr<<"Undo did not restore the note\n";return 75;}
 {juce::MidiBuffer mb;juce::AudioBuffer<float> b(2,256);for(int blk=0;blk<40;++blk){for(int i2=0;i2<256;++i2)for(int c2=0;c2<2;++c2)b.setSample(c2,i2,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(blk*256+i2)/48000));visual->processBlock(b,mb);}}
 const int targetUndone=megatune::historyDecision(*visual,megatune::historySequence(*visual)-1).target;
 if(targetUndone!=69){std::cerr<<"Undo did not restore the target\n";return 76;}
 megatune::appendDecision(*visual,0,0,3);pumpEditor();dynamic_cast<juce::Button*>(row0)->onClick();pumpEditor();
 if(enableButton->isEnabled()||excludeButton->isEnabled()){std::cerr<<"No-target decision offered an edit action\n";return 77;}
 auto* engine=paramById("engine");engine->setValueNotifyingHost(1.f);pumpEditor();
 if(experimental->isVisible()){std::cerr<<"Experimental label shown for TD-PSOLA\n";return 78;}
 engine->setValueNotifyingHost(0.f);pumpEditor();
 if(experimental->isVisible()){std::cerr<<"Experimental label shown after TD-PSOLA off\n";return 79;}
 dynamic_cast<juce::Button*>(closeButton)->onClick();
 if(panel->isVisible()||!speedControl->isVisible()){std::cerr<<"Close did not restore covered controls\n";return 80;}
 for(const char* id:{"control-sustain","control-amount","control-doubler","control-width","control-reference","control-stabilize"})
  {auto* covered=findById(editor.get(),id);if(!covered||!covered->isVisible()){std::cerr<<"Close did not restore: "<<id<<"\n";return 82;}}
 if(std::abs(paramById("speed")->getValue()-speedBefore)>.0001f){std::cerr<<"Close changed a control value\n";return 81;}
 auto* browse=findById(editor.get(),"preset-browse");if(!browse){std::cerr<<"Preset browser button missing\n";return 108;}
 dynamic_cast<juce::Button*>(browse)->onClick();
 auto* browser=findById(editor.get(),"preset-browser");if(!browser||!browser->isVisible()){std::cerr<<"Preset browser did not open\n";return 109;}
 writeShot("MegaTune-vst3-presets.png");
 dynamic_cast<juce::Button*>(browser->findChildWithID("preset-load"))->onClick();
 if(!editor->isResizable()){std::cerr<<"Editor is not resizable\n";return 110;}
 editor->setSize(800,573);if(editor->getWidth()!=800||editor->getHeight()!=573){std::cerr<<"Editor resize failed\n";return 111;}
 editor->setSize(1120,802);
 for(auto* parameter:p->getParameters()){auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);if(identified&&identified->paramID.startsWith("note"))parameter->setValueNotifyingHost(identified->paramID=="note6"?1.f:0.f);}steady.clear();for(int block=0;block<200;++block){for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch)buffer.setSample(ch,i,.3f*std::sin(juce::MathConstants<double>::twoPi*432*(block*256+i)/48000));p->processBlock(buffer,midi);if(block>100)for(int i=0;i<256;++i)steady.push_back(buffer.getSample(0,i));}std::vector<double> sc(115,0.0);bestScore=-1;for(int lag=95;lag<112;++lag){double cross=0;for(int i=0;i<16000;++i)cross+=steady[i]*steady[i+lag];sc[lag]=cross;if(cross>bestScore){bestScore=cross;bestLag=lag;}}const double den=sc[bestLag-1]-2*sc[bestLag]+sc[bestLag+1];const double off=std::abs(den)>1e-12?std::clamp(.5*(sc[bestLag-1]-sc[bestLag+1])/den,-.5,.5):0.0;frequency=48000.0/(bestLag+off);std::cout<<"Disabled-target stabilizer frequency: "<<frequency<<" Hz\n";if(std::abs(frequency-466.16)>4)return 8;rangedSpeed->setValueNotifyingHost(rangedSpeed->convertTo0to1(25));rangedAmount->setValueNotifyingHost(rangedAmount->convertTo0to1(75));
 juce::MemoryBlock state;p->getStateInformation(state);if(state.getSize()==0)return 4;std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
 auto* restoredSpeed=dynamic_cast<juce::RangedAudioParameter*>(restored->getParameters()[1]);
 auto* restoredAmount=dynamic_cast<juce::RangedAudioParameter*>(restored->getParameters()[3]);
 if(!restoredSpeed||!restoredAmount)return 15;
 restoredSpeed->setValueNotifyingHost(restoredSpeed->convertTo0to1(200));restoredAmount->setValueNotifyingHost(0);
 restored->setStateInformation(state.getData(),(int)state.getSize());
 if(std::abs(restoredSpeed->convertFrom0to1(restoredSpeed->getValue())-25)>.001f || std::abs(restoredAmount->convertFrom0to1(restoredAmount->getValue())-75)>.001f)return 16;
 p->setStateInformation(state.getData(),(int)state.getSize());for(auto* parameter:p->getParameters()){if(parameter->getName(64)=="Global Bypass")parameter->setValueNotifyingHost(1);}
p->prepareToPlay(48000,256);for(int block=0;block<20;++block){buffer.clear();if(block==0){buffer.setSample(0,0,.7f);buffer.setSample(1,0,.7f);}p->processBlock(buffer,midi);if(block==8&&std::abs(buffer.getSample(0,0)-.7f)>.00001f)return 5;}p->prepareToPlay(48000,256);for(int block=0;block<20;++block){buffer.clear();if(block==0)buffer.setSample(0,0,.6f);p->processBlockBypassed(buffer,midi);if(block==8&&std::abs(buffer.getSample(0,0)-.6f)>.00001f)return 9;}if(p->getBypassParameter()==nullptr)return 10;std::cout<<"PASS: streaming finite audio, state round-trip, name and latency-aligned bypass\n";return 0;}
