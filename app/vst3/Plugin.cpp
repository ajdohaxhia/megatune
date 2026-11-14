#include <juce_audio_utils/juce_audio_utils.h>
#if (JUCE_MAJOR_VERSION * 10000 + JUCE_MINOR_VERSION * 100 + JUCE_BUILDNUMBER) < 80013
 #error "MegaTune requires JUCE 8.0.13 or newer (createEditorAndMakeActive was added in 8.0.13)."
#endif
#include <array>
#include "EditorApi.h"
#include "TuningRules.h"
#include "TuningDsp.h"
#include "third_party/signalsmith/signalsmith-stretch.h"
#include <cmath>
#include <vector>
std::atomic<int> megatune::lastTargetPc{-1};
std::atomic<unsigned> megatune::debugMask{0};
std::atomic<int> megatune::debugNearest{-1}, megatune::debugMidi{0}, megatune::debugKey{-1};
class MegaTune final : public juce::AudioProcessor {
public:
 juce::AudioProcessorValueTreeState state;
 static juce::AudioProcessorValueTreeState::ParameterLayout parameters() {
  juce::AudioProcessorValueTreeState::ParameterLayout p;
  auto f=[&](const char* id,const char* name,float lo,float hi,float def){p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,0.1f),def));};
  p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"key",1},"Key",juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},4));
  auto speedRange=juce::NormalisableRange<float>(-3,200,
   [](float,float,float n){return megaTuneSpeedFromNormalized(n);},
   [](float,float,float v){return megaTuneSpeedToNormalized(v);});
  auto amountRange=juce::NormalisableRange<float>(0,100,
   [](float,float,float n){return megaTuneAmountFromNormalized(n);},
   [](float,float,float v){return megaTuneAmountToNormalized(v);});
  p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"speed",1},"Speed",speedRange,originalControls::speedDefault));
  f("sustain","Sustain",-100,100,originalControls::sustainDefault);
  p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount",1},"Amount",amountRange,originalControls::amountDefault));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Global Bypass",false));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"stabilize",1},"Note Stabilizer",true));
  const char* notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  for(int i=0;i<12;++i)p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"note"+juce::String(i),1},notes[i],i==0||i==2||i==3||i==5||i==7||i==8||i==10));
  f("doubler","Doubler",0,100,originalControls::doublerDefault);f("width","Width",0,100,originalControls::widthDefault);
  f("reference","Reference",415,466,440);p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"lowlatency",1},"Low Latency",false));
  p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"shift",1},"Shift",juce::NormalisableRange<float>(-12,12,1.f),0.f));
  p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"range",1},"Range",juce::NormalisableRange<float>(1,12,1.f),12.f));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"engine",1},"TD-PSOLA",true));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"smith",1},"Signalsmith",false));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"smithcomp",1},"Smith Comp",false));
  return p;
 }
 MegaTune():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"MegaTune",parameters()){const char* ids[]={"speed","sustain","amount","bypass","stabilize","key","doubler","width","reference","lowlatency","shift","range","engine","smith","smithcomp"};for(int i=0;i<15;++i)raw[i]=state.getRawParameterValue(ids[i]);for(int i=0;i<12;++i)noteRaw[i]=state.getRawParameterValue("note"+juce::String(i));}
 const juce::String getName()const override{return "MegaTune";}
 bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;}
 // The plug-in holds its latency's worth of audio plus the Doubler's delay lines. A host that stops
 // calling it as soon as the input ends (Logic between regions, FL Studio's smart disable) would cut
 // that off and get it back, stale, when processing resumes: a click at the start of the next phrase.
 double getTailLengthSeconds()const override{return rate>0?(double)getLatencySamples()/rate+.06:.2;}
 std::atomic<int> currentProgram{0};
 struct FactoryPreset{const char* name;const char* category;const char* about;float speed,sustain,amount,doubler,width;bool stabilize;int key;int mask;};
 static constexpr int numFactoryPresets=32;
 // Order matters to hosts and saved sessions: only append. Key/mask are applied
 // only when "Keep my key and scale" is off.
 static const FactoryPreset* factoryPresets(){static const FactoryPreset p[]={
  {"Default","Start","Fast, full correction. A neutral place to start.",25,0,100,0,0,true,4,1453},
  {"Natural Tune","Natural","Gentle correction that keeps slides and vibrato.",90,-20,70,0,0,true,0,2741},
  {"Modern Pop","Pop","Tight and polished with a touch of width.",45,-10,95,8,12,true,0,2741},
  {"Polished Pop","Pop","Slightly slower than Modern Pop, a little wider.",70,-25,85,10,20,true,0,2741},
  {"R&B Smooth","R&B","Slow retune that lets runs and bends breathe.",120,-40,75,15,25,true,9,1453},
  {"Hip-Hop Lead","Hip-Hop","Fast, obvious tuning for a rap or sung lead.",25,10,100,5,10,true,0,1453},
  {"Doubler Wide","Doubles","Tuned lead with a wide double on both sides.",25,0,100,65,100,true,0,2741},
  {"Trap Robotic","Hip-Hop","Hard tune: notes snap with no glide.",-3,40,100,0,0,false,0,4095},
  {"EDM Vocal","Pop","Very fast and bright, with a wide double.",15,20,100,20,45,true,0,2741},
  {"Rock Anthem","Rock","Firm correction that still sounds sung.",60,-15,90,12,30,true,4,1453},
  {"Indie Folk","Natural","Slow and partial. Leaves the performance alone.",150,-45,60,0,0,true,0,2741},
  {"Lo-Fi Bed","Effect","Loose tuning under a wide, blurry double.",200,-60,55,35,60,true,0,2741},
  {"Doubler Tight","Doubles","A close double that thickens without spreading.",30,0,100,35,15,true,0,2741},
  {"Subtle Assist","Natural","Barely there. Nudges only the worst notes.",180,-30,40,0,0,true,0,2741},
  {"Extreme Tune","Effect","Hard tune that locks even harder on held notes.",-3,60,100,0,0,false,0,4095},
  {"Spoken Word","Natural","Light chromatic correction for half-sung lines.",100,-10,50,0,0,true,0,4095},
  {"Transparent Lead","Natural","Full-strength but unhurried. Hard to hear working.",60,-30,80,0,0,true,0,2741},
  {"Ballad","Natural","Long notes settle slowly; vibrato stays intact.",130,-50,65,6,10,true,0,2741},
  {"Live Safety Net","Natural","Half correction for takes that are nearly there.",110,-20,55,0,0,true,0,2741},
  {"Radio Ready","Pop","Fast full correction with a narrow double.",30,0,100,6,15,true,0,2741},
  {"Stacked Chorus","Doubles","Sounds like a chorus stack from one take.",40,-10,95,50,70,true,0,2741},
  {"Background Vox","Doubles","Very tight and very wide, for parts behind a lead.",20,10,100,45,85,true,0,2741},
  {"Melodic Rap","Hip-Hop","Quick glide between notes, firm on held ones.",8,20,100,8,18,true,0,1453},
  {"Hard Tune Classic","Effect","Instant retune with the stabilizer on.",0,30,100,0,0,true,0,1453},
  {"Drill Hook","Hip-Hop","Near-instant tuning with a wide double.",3,25,100,12,30,true,0,1453},
  {"Neo Soul","R&B","Very slow and partial. Keeps scoops and falls.",140,-55,65,10,20,true,0,1453},
  {"Silky Falsetto","R&B","Medium retune with an airy, wide double.",85,-35,80,18,35,true,0,1453},
  {"Pentatonic Lock","Effect","Hard tune. Turn key lock off to snap to a pentatonic.",-3,50,100,0,0,false,0,661},
  {"Punk Shout","Rock","Fast enough to fix it, rough enough to stay human.",35,0,90,20,40,true,4,1453},
  {"Arena Gang","Rock","Firm tuning with a big wide double.",50,-10,90,40,80,true,4,1453},
  {"Wide Whisper","Effect","Light tuning; mostly stereo width.",160,-40,50,30,100,true,0,2741},
  {"Mono Double","Doubles","A thick double that stays in the centre.",35,0,100,55,0,true,0,2741}
 };return p;}
 // When true (the default) presets change the sound but leave the song's key and scale alone.
 std::atomic<bool> keepKey{true};
 juce::String getUserPresetName()const{const juce::SpinLock::ScopedLockType l(nameLock);return userPresetName;}
 void setUserPresetName(const juce::String& n){const juce::SpinLock::ScopedLockType l(nameLock);userPresetName=n;}
 void setParam(const juce::String& id,float plain){if(auto* param=state.getParameter(id)){param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1(plain));param->endChangeGesture();}}
 int getNumPrograms()override{return numFactoryPresets;} int getCurrentProgram()override{return currentProgram.load();}
 const juce::String getProgramName(int i)override{return factoryPresets()[juce::jlimit(0,numFactoryPresets-1,i)].name;}
 void setCurrentProgram(int i)override{if(i<0||i>=getNumPrograms())return;currentProgram=i;setUserPresetName({});const auto&p=factoryPresets()[i];
  setParam("speed",p.speed);setParam("sustain",p.sustain);setParam("amount",p.amount);setParam("doubler",p.doubler);setParam("width",p.width);
  setParam("stabilize",p.stabilize?1.f:0.f);
  if(!keepKey.load()){setParam("key",(float)p.key);for(int n=0;n<12;++n)setParam("note"+juce::String(n),(p.mask&(1<<n))?1.f:0.f);}}
 // User presets: one XML file per sound, shared by every instance and host.
 static juce::File userPresetDir(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support").getChildFile("ademusic").getChildFile("MegaTune").getChildFile("Presets");}
 static juce::StringArray userPresetNames(){juce::StringArray names;for(auto& f:userPresetDir().findChildFiles(juce::File::findFiles,false,"*.megatune"))names.add(f.getFileNameWithoutExtension());names.sort(true);return names;}
 bool saveUserPreset(const juce::String& rawName){const auto name=juce::File::createLegalFileName(rawName.trim());if(name.isEmpty())return false;
  const auto dir=userPresetDir();if(!dir.createDirectory().wasOk())return false;
  auto xml=state.copyState().createXml();if(xml==nullptr||!xml->writeTo(dir.getChildFile(name+".megatune")))return false;
  setUserPresetName(name);return true;}
 bool loadUserPreset(const juce::String& name){auto xml=juce::XmlDocument::parse(userPresetDir().getChildFile(name+".megatune"));
  if(xml==nullptr||!xml->hasTagName(state.state.getType()))return false;const auto tree=juce::ValueTree::fromXml(*xml);const bool keep=keepKey.load();
  for(const auto& child:tree){const auto id=child.getProperty("id").toString();if(id.isEmpty()||id=="bypass"||!child.hasProperty("value"))continue;
   if(keep&&(id=="key"||id.startsWith("note")))continue;setParam(id,(float)child.getProperty("value"));}
  setUserPresetName(name);return true;}
 bool deleteUserPreset(const juce::String& name){const auto file=userPresetDir().getChildFile(name+".megatune");if(!file.existsAsFile()||!file.deleteFile())return false;
  if(getUserPresetName()==name)setUserPresetName({});return true;}
void changeProgramName(int,const juce::String&)override{}
 bool isBusesLayoutSupported(const BusesLayout& l)const override{const auto in=l.getMainInputChannelSet(),out=l.getMainOutputChannelSet();
  // mono -> mono, stereo -> stereo, and mono -> stereo so a mono vocal can come out doubled and wide
  return (in==out&&(out==juce::AudioChannelSet::mono()||out==juce::AudioChannelSet::stereo()))||(in==juce::AudioChannelSet::mono()&&out==juce::AudioChannelSet::stereo());}
 void prepareToPlay(double r,int samplesPerBlock)override {rate=r;write=0;analysisCount=0;phase=0;ratio=1;target=0;lastNote=0;heldSeconds=0;currentTargetNote=0;psolaVoiced=false;voicedHold=0;pitchCount=0;for(auto& c:ring)c.fill(0); history.fill(0);hist=0;lowLatencyActive=value("lowlatency")>.5f;window=lowLatencyActive?512:2048;setLatencySamples(window);yinWorkspace.ensure(512);hop=juce::jmax(64,(int)std::lround(r*hopSeconds));refineSpan=juce::jmax(256,(int)std::lround(r*.032));refineScratch.assign((size_t)(refineSpan+(int)(r/50.0)+96),0.f);bypassMix=value("bypass")>.5f?1.f:0.f;dryMix=(bypassMix>.5f||(value("amount")==0&&value("shift")==0))?1.f:0.f;targetRatio=1;ratioFor=0;psola.prepare(2048,r);psola.setLatency(window);doubler.prepare(r);if(!smithReady||smithRate!=r){smith.presetDefault(2,(float)rate);smithReady=true;smithRate=r;}smith.reset();smithLatency=smith.inputLatency()+smith.outputLatency();if(value("smith")>.5f&&!lowLatencyActive)setLatencySamples(smithLatency);const int staging=juce::jmax(8192,samplesPerBlock);sIn[0].assign((size_t)staging,0.f);sIn[1].assign((size_t)staging,0.f);sOut[0].assign((size_t)staging,0.f);sOut[1].assign((size_t)staging,0.f);}
 void releaseResources()override{}
 void reset()override{write=0;hist=0;analysisCount=0;phase=0;ratio=1;target=0;lastNote=0;heldSeconds=0;currentTargetNote=0;pitch=0;outputPitch=0;level=0;outputLevel=0;pitchCount=0;settleMidi=0;pitchHist[0]=pitchHist[1]=pitchHist[2]=0;psolaPeriod=200;psolaVoiced=false;voicedHold=0;for(auto& c:ring)c.fill(0);history.fill(0);psola.reset();doubler.reset();if(smithReady)smith.reset();}
 float value(const char* id) const{const char* ids[]={"speed","sustain","amount","bypass","stabilize","key","doubler","width","reference","lowlatency","shift","range","engine","smith","smithcomp"};for(int i=0;i<15;++i)if(std::strcmp(id,ids[i])==0)return raw[i]->load();auto* v=state.getRawParameterValue(id);return v?v->load():0;}
 float noteValue(int i)const{return noteRaw[i]->load();}
 int smithBufferCapacity()const{return (int)std::min(sIn[0].capacity(),sOut[0].capacity());}
 juce::AudioProcessorParameter* getBypassParameter()const override{return state.getParameter("bypass");}
 enum DecisionReason{R_NEAREST=0,R_HELD=1,R_RANGE=2,R_UNVOICED=3,R_NONOTE=4};
 struct Decision{std::atomic<float> in{0.f},target{0.f};std::atomic<int> reason{0};};
 std::array<Decision,16> decisions{};std::atomic<int> historySeq{0};std::atomic<unsigned> historyLock{0};std::atomic<int> currentTargetNote{0};
 void recordDecision(float in,float target,int reason){
  megatune::lastTargetPc.store(target>0?(((int)std::lround(target))%12+12)%12:-1,std::memory_order_relaxed);
  const unsigned l=historyLock.load(std::memory_order_relaxed);
  historyLock.store(l+1u,std::memory_order_release);           // odd: write in progress
  const int i=historySeq.load(std::memory_order_relaxed)&15;
  decisions[(size_t)i].in.store(in,std::memory_order_relaxed);
  decisions[(size_t)i].target.store(target,std::memory_order_relaxed);
  decisions[(size_t)i].reason.store(reason,std::memory_order_relaxed);
  historyLock.store(l+2u,std::memory_order_release);           // even: consistent
  historySeq.fetch_add(1,std::memory_order_release);
 }
 bool readDecision(int sequence,megatune::DecisionView& out)const{
  for(int tries=0;tries<16;++tries){
   const unsigned l1=historyLock.load(std::memory_order_acquire);
   if(l1&1u) continue;
   const auto& e=decisions[(size_t)(sequence&15)];
   const float in=e.in.load(std::memory_order_relaxed);
   const float tg=e.target.load(std::memory_order_relaxed);
   const int rs=e.reason.load(std::memory_order_relaxed);
   if(historyLock.load(std::memory_order_acquire)==l1){out={in,tg,rs};return true;}
  }
  return false;
 }
 std::atomic<float> pitch{0},outputPitch{0},level{0},outputLevel{0};
 void analyse(){
  constexpr int n=768; std::array<float,n> x{};float energy=0;
  const int stride=juce::jmax(1,(int)(rate/12000));
  // box-filtered decimation (anti-aliased) of the newest samples, newest first
  for(int i=0;i<n;++i){float s=0;const int base=hist-1-i*stride+16384*16;for(int k=0;k<stride;++k)s+=history[(size_t)((base-k)%16384)];x[i]=s/(float)stride;energy+=x[i]*x[i];}
  if(energy/n<0.00001f){pitch=0;target=0;lastNote=0;heldSeconds=0;pitchCount=0;currentTargetNote=0;settleMidi=0;recordDecision(0,0,R_UNVOICED);return;}
  const float sr=(float)rate/stride;
  const auto yin=megatune::detectYin(x.data(),n,sr,55.f,1200.f,.15f,yinWorkspace);
  if(!yin.voiced){pitch=0;target=0;lastNote=0;heldSeconds=0;pitchCount=0;currentTargetNote=0;settleMidi=0;recordDecision(0,0,R_UNVOICED);return;}
  // Refine the period on the full-rate signal, on a span centred on the audio that
  // is leaving the shifter right now (write - latency), so the correction lines up
  // with the grains it is applied to instead of running ahead of them.
  float period=(float)rate/juce::jmax(1.f,yin.hz);
  {const int coarse=(int)std::lround(period);const int radius=juce::jlimit(stride+2,30,(int)std::ceil(period*.035f));
   // The span is centred on the audio leaving the shifter. In Low Latency that leaves room for less than
   // the full span, so it is shortened rather than letting the estimate lag the grains it corrects.
   int span=juce::jmin(refineSpan,(int)refineScratch.size()-coarse-radius-2,2*window-coarse-radius-2);span=juce::jmax(span,juce::jmin(256,(int)refineScratch.size()-coarse-radius-2));
   if(span>=128){const int total=span+coarse+radius+2;const int offset=juce::jlimit(0,16384-total-1,window-total/2);
    for(int i=0;i<total;++i)refineScratch[(size_t)i]=history[(size_t)((hist-1-offset-i+16384*4)%16384)];
    period=megatune::refinePeriod(refineScratch.data(),span,period,radius);}}
  const float hz=(float)rate/juce::jmax(2.f,period);
  float midi=69+12*std::log2(hz/juce::jmax(1.f,value("reference")));
  // A pitch is only acted on once two frames in a row agree. The frames where a note starts or changes
  // often report a sub-harmonic or a value in between, and following those detunes and dips the onset.
  {const bool settled=settleMidi>0&&std::abs(midi-settleMidi)<1.f;settleMidi=midi;if(!settled)return;}
  pitch=midi;
  if(pitchCount<3)pitchHist[pitchCount++]=midi;else{pitchHist[0]=pitchHist[1];pitchHist[1]=pitchHist[2];pitchHist[2]=midi;const float lo=std::min(pitchHist[0],std::min(pitchHist[1],pitchHist[2])),hi=std::max(pitchHist[0],std::max(pitchHist[1],pitchHist[2]));const float med=pitchHist[0]+pitchHist[1]+pitchHist[2]-lo-hi;if(std::abs(midi-med)>1.5f){midi=med;period=(float)(rate/(juce::jmax(1.f,value("reference"))*std::pow(2.0,(med-69.0)/12.0)));}} // median only rejects octave slips; it no longer delays every frame
  float distance=100;int nearest=0;for(int i=(int)std::floor(midi)-12;i<=(int)std::ceil(midi)+12;++i)if(noteValue((i-(int)value("key")+24)%12)>.5f&&std::abs(i-midi)<distance){distance=std::abs(i-midi);nearest=i;}
  if(distance==100){target=0;lastNote=0;heldSeconds=0;currentTargetNote=0;recordDecision(midi,0,R_NONOTE);return;}if(value("stabilize")>.5f&&lastNote>0&&std::abs(midi-lastNote)<distance+.2f&&noteValue((lastNote-(int)value("key")+24)%12)>.5f)nearest=lastNote;{unsigned m=0;for(int q=0;q<12;++q)if(noteRaw[q]->load()>.5f)m|=1u<<q;megatune::debugMask.store(m);megatune::debugNearest.store(nearest);megatune::debugMidi.store((int)std::lround(midi*100.f));megatune::debugKey.store((int)value("key"));}const int previousNote=lastNote;if(nearest!=lastNote)heldSeconds=0;const bool stabilizeOn=value("stabilize")>.5f;lastNote=nearest;currentTargetNote=nearest;const bool held=(stabilizeOn&&nearest>0&&nearest==previousNote);const float correction=(float)(nearest-midi);const bool outOfRange=std::abs(correction)>value("range");
  target=(outOfRange?0.f:correction)*value("amount")*.01f+value("shift");
  recordDecision(midi,(float)nearest,outOfRange?R_RANGE:(held?R_HELD:R_NEAREST));psolaPeriod=period;psola.setPeriod(psolaPeriod);voicedHold=(int)(rate*0.06);
 }
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{spreadMono(b);processAudio(b,false);}
 void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{spreadMono(b);processAudio(b,true);}
 // Mono in, stereo out: the host leaves the second channel undefined, so the mono input is copied
 // into it and the rest of the plug-in runs as stereo (Doubler and Width then work as on a stereo track).
 void spreadMono(juce::AudioBuffer<float>& b){if(getTotalNumInputChannels()==1&&getTotalNumOutputChannels()>=2&&b.getNumChannels()>=2)b.copyFrom(1,0,b,0,0,b.getNumSamples());}
 void processAudio(juce::AudioBuffer<float>& b,bool hostBypass){
  const bool smithActive=value("smith")>.5f&&!lowLatencyActive;
  if(!hostBypass&&value("bypass")<=.5f&&smithActive){const int n=b.getNumSamples();
   if(n>(int)sIn[0].size()){
    // block larger than the preallocated staging area: process in slices, never allocate here
    const int cap=juce::jmax(1,(int)sIn[0].size());
    for(int off=0;off<n;off+=cap){const int m=juce::jmin(cap,n-off);
     float* ptrs[2]={b.getWritePointer(0,off),b.getWritePointer(b.getNumChannels()>1?1:0,off)};
     juce::AudioBuffer<float> slice(ptrs,b.getNumChannels(),m);processAudio(slice,hostBypass);}
    return;}
   const int channels=juce::jmin(2,b.getNumChannels());if(channels<=0)return;const int second=juce::jmin(1,channels-1);float peak=0;
   for(int i=0;i<n;++i){float mono=0;for(int c=0;c<channels;++c){const float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0.f;mono+=ring[c][write]/channels;peak=juce::jmax(peak,std::abs(ring[c][write]));}
    history[hist]=mono;hist=(hist+1)%16384;if(++analysisCount>=hop){analysisCount=0;analyse();}write=(write+1)%ringSize;
    const float in0=b.getSample(0,i),in1=b.getSample(second,i);sIn[0][i]=std::isfinite(in0)?in0:0.f;sIn[1][i]=std::isfinite(in1)?in1:0.f;}
   if(lastNote>0)heldSeconds=juce::jmin(1.f,heldSeconds+(float)n/(float)rate);
   {const float blend=megaTuneRatioBlend(value("speed"),value("sustain"),heldSeconds,(float)rate);ratio+=(std::pow(2.f,target/12)-ratio)*(1.f-std::pow(1.f-blend,(float)n));}
   smith.setTransposeSemitones(12.f*std::log2(juce::jmax(.001f,ratio)),0.f);
   dryMix=bypassMix=0;smith.setFormantSemitones(0.f,false); // formant compensation disabled: measured to cancel pitch shift (see audit)
   const float* ip[2]={sIn[0].data(),sIn[1].data()};float* op[2]={sOut[0].data(),sOut[1].data()};
   smith.process(ip,n,op,n);
   float outPeak=0;const float dblAmount=value("doubler")*.01f,dblWidth=value("width")*.01f;
   for(int i=0;i<n;++i){float o[2];for(int c=0;c<2;++c){o[c]=sOut[(size_t)juce::jmin(c,channels-1)][i];if(!std::isfinite(o[c]))o[c]=0.f;}
    doubler.process(o[0],o[1],dblAmount,dblWidth,channels>1);
    for(int c=0;c<channels;++c){outPeak=juce::jmax(outPeak,std::abs(o[c]));b.setSample(c,i,o[c]);}}
   level=peak;outputLevel=outPeak;outputPitch=pitch.load()>0?pitch.load()+target:0.f;
   if(getLatencySamples()!=smithLatency)setLatencySamples(smithLatency);
   return;}


  juce::ScopedNoDenormals no;const int channels=juce::jmin(2,b.getNumChannels());if(channels<=0)return;float peak=0,outPeak=0;const bool bypass=hostBypass||value("bypass")>.5f;
  const int requestedWindow=value("lowlatency")>.5f?512:2048;
  if(smithActive){if(getLatencySamples()!=smithLatency)setLatencySamples(smithLatency);}
  else if(requestedWindow!=window){window=requestedWindow;lowLatencyActive=window==512;setLatencySamples(window);psola.setLatency(window);}
  const int half=window/2;const int dryDelay=smithActive?smithLatency:window;
  // Parameters are read once per block; the fades below keep a change from clicking.
  const float speedV=value("speed"),sustainV=value("sustain"),amountV=value("amount"),shiftV=value("shift"),doublerV=value("doubler")*.01f,widthV=value("width")*.01f;
  const bool psolaEngine=value("engine")>.5f;const float dryTarget=(bypass||(amountV==0&&shiftV==0))?1.f:0.f,bypassTarget=bypass?1.f:0.f,fadeStep=1.f/(float)(.005*rate);
  if(smithActive)dryMix=bypassMix=1.f; // only reached bypassed: the Signalsmith path has a different delay, so no cross-fade
  float blend=0;
  for(int i=0;i<b.getNumSamples();++i){float mono=0;for(int c=0;c<channels;++c){float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0;mono+=ring[c][write]/channels;peak=juce::jmax(peak,std::abs(ring[c][write]));}history[hist]=mono;hist=(hist+1)%16384;
   if(++analysisCount>=hop){analysisCount=0;analyse();}
   if(lastNote>0)heldSeconds=juce::jmin(1.f,heldSeconds+1.f/(float)rate);
   if((i&15)==0)blend=megaTuneRatioBlend(speedV,sustainV,heldSeconds,(float)rate);
   if(target!=ratioFor){ratioFor=target;targetRatio=std::pow(2.f,target/12);}
   ratio+=(targetRatio-ratio)*blend;
   phase+=1-ratio;while(phase<0)phase+=window;while(phase>=window)phase-=window;
   if(voicedHold>0)--voicedHold;psolaVoiced=voicedHold>0;
   dryMix+=juce::jlimit(-fadeStep,fadeStep,dryTarget-dryMix);bypassMix+=juce::jlimit(-fadeStep,fadeStep,bypassTarget-bypassMix);
   const int second=channels>1?1:0;const float psIn[2]={ring[0][write],ring[second][write]};float ps[2];
   psola.setRatio(ratio);psola.process(psIn,ps,psolaVoiced);   // Speed-smoothed ratio drives the shifter
   float outs[2]={0,0};
   for(int c=0;c<channels;++c){auto read=[&](float delay){float pos=write-delay;while(pos<0)pos+=ringSize;int a=(int)pos;float t=pos-a;
     auto tap=[&](int k){return ring[c][(a+k+ringSize)%ringSize];};float y0=tap(-1),y1=tap(0),y2=tap(1),y3=tap(2);
     return y1+.5f*t*(y2-y0+t*(2.f*y0-5.f*y1+4.f*y2-y3+t*(3.f*(y1-y2)+y3-y0)));};float out;
    if(dryMix>=1.f)out=read((float)dryDelay);
    else{float wet;if(psolaEngine)wet=ps[c];
     else if(lowLatencyActive){float p2=std::fmod(phase+half,(float)window);float w=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*phase/window);wet=read(half+phase)*w+read(half+p2)*(1-w);}
     else{const float base=(float)(half+window/4);float acc=0;for(int k=0;k<4;++k){float g=std::fmod(phase+k*window*.25f,(float)window);float wt=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*g/window);acc+=wt*read(base+g);}wet=acc*.5f;}
     out=dryMix<=0.f?wet:wet+dryMix*(read((float)dryDelay)-wet);}
    outs[c]=out;}
   if(channels==1)outs[1]=outs[0];
   doubler.process(outs[0],outs[1],doublerV*(1.f-bypassMix),widthV*(1.f-bypassMix),channels>1);
   for(int c=0;c<channels;++c){outPeak=juce::jmax(outPeak,std::abs(outs[c]));b.setSample(c,i,outs[c]);}
   write=(write+1)%ringSize;
  }level=peak;outputLevel=outPeak;float detected=pitch.load();outputPitch=detected>0?detected+((bypass||amountV==0)?0.f:12.f*std::log2(juce::jmax(.001f,ratio))):0.f;
 }
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override;
 void getStateInformation(juce::MemoryBlock& m)override{auto saved=state.copyState();saved.setProperty("factoryProgram",currentProgram.load(),nullptr);saved.setProperty("keepKey",keepKey.load(),nullptr);saved.setProperty("userPreset",getUserPresetName(),nullptr);auto xml=saved.createXml();copyXmlToBinary(*xml,m);}
 void setStateInformation(const void* d,int n)override{auto xml=getXmlFromBinary(d,n);if(xml&&xml->hasTagName(state.state.getType())){auto saved=juce::ValueTree::fromXml(*xml);currentProgram=juce::jlimit(0,getNumPrograms()-1,(int)saved.getProperty("factoryProgram",0));keepKey=(bool)saved.getProperty("keepKey",true);setUserPresetName(saved.getProperty("userPreset",juce::String()).toString());state.replaceState(saved);}}
private:
 mutable juce::SpinLock nameLock;juce::String userPresetName;
 std::array<std::atomic<float>*,15> raw{};std::array<std::atomic<float>*,12> noteRaw{};
 double rate=44100;int write=0,hist=0,analysisCount=0,lastNote=0,window=2048,pitchCount=0;float pitchHist[3]{};megatune::StereoPsola psola;megatune::StereoDoubler doubler;std::vector<float> refineScratch;int hop=256,refineSpan=1536;float dryMix=0,bypassMix=0,targetRatio=1,ratioFor=0,settleMidi=0;static constexpr double hopSeconds=0.010667;float psolaPeriod=200.f;bool psolaVoiced=false;int voicedHold=0;signalsmith::stretch::SignalsmithStretch<float> smith;bool smithReady=false;double smithRate=0;std::vector<float> sIn[2],sOut[2];int smithLatency=0;megatune::YinWorkspace yinWorkspace;bool lowLatencyActive=false;float phase=0,ratio=1,target=0,heldSeconds=0;
 static constexpr int ringSize=131072;std::array<std::array<float,ringSize>,2> ring{};std::array<float,16384> history{};
};
class TunerLook final:public juce::LookAndFeel_V4 {
public:
 static constexpr juce::uint32 accent=0xffa78bfa,accentDeep=0xff7c5cff,ink=0xfff4f1ff,muted=0xff8d879d,line=0xff2d2939,panel=0xff17151f;
 void drawButtonBackground(juce::Graphics& g,juce::Button& button,const juce::Colour&,bool over,bool down)override{
  auto r=button.getLocalBounds().toFloat().reduced(1.f);const bool on=button.getToggleState();const auto id=button.getComponentID();
  const bool primary=id=="history-enable"||id=="history-exclude"||id=="preset-load";const bool outline=id=="history-undo";
  juce::Colour fill=primary?juce::Colour(accent):(outline?juce::Colour(accent).withAlpha(.08f):(on?juce::Colour(accent).withAlpha(.24f):juce::Colour(0xff1c1a25)));
  if(over)fill=fill.brighter(.08f);if(down)fill=fill.darker(.08f);if(!button.isEnabled())fill=fill.withAlpha(.45f);
  g.setColour(fill);g.fillRoundedRectangle(r,6.f);
  g.setColour(primary?juce::Colour(accent):(outline?juce::Colour(accent).withAlpha(.7f):(on?juce::Colour(accent).withAlpha(.85f):juce::Colour(line))));g.drawRoundedRectangle(r,6.f,1.f);
 }
 void drawButtonText(juce::Graphics& g,juce::TextButton& button,bool,bool)override{
  g.setFont(juce::Font(juce::FontOptions(12.f)));const auto id=button.getComponentID();
  g.setColour((id=="history-enable"||id=="history-exclude"||id=="preset-load")?juce::Colour(0xff17121f):(id=="history-undo"?juce::Colour(accent):(button.getToggleState()?juce::Colour(ink):juce::Colour(0xffd7d2e4).withAlpha(button.isEnabled()?1.f:.45f))));
  g.drawFittedText(button.getButtonText(),button.getLocalBounds().reduced(5,0),juce::Justification::centred,1);
 }
 void drawComboBox(juce::Graphics& g,int width,int height,bool down,int,int,int,int,juce::ComboBox& box)override{
  auto r=juce::Rectangle<float>(0,0,(float)width,(float)height).reduced(1.f);
  g.setColour(box.findColour(juce::ComboBox::backgroundColourId));g.fillRoundedRectangle(r,6.f);
  g.setColour(down?juce::Colour(accent):juce::Colour(line));g.drawRoundedRectangle(r,6.f,1.f);
  juce::Path p;p.startNewSubPath((float)width-18.f,(float)height*.5f-2.f);p.lineTo((float)width-13.f,(float)height*.5f+3.f);p.lineTo((float)width-8.f,(float)height*.5f-2.f);
  g.setColour(juce::Colour(muted));g.strokePath(p,juce::PathStrokeType(1.6f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
 }
 void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,bool over,bool)override{
  auto r=button.getLocalBounds().toFloat();auto box=r.removeFromLeft(22.f).withSizeKeepingCentre(18.f,18.f);
  const bool on=button.getToggleState();
  g.setColour(on?juce::Colour(accent):juce::Colour(0xff24212f));g.fillRoundedRectangle(box,5.f);
  g.setColour(on?juce::Colour(accent):juce::Colour(0xff3a3548));g.drawRoundedRectangle(box,5.f,1.f);
  if(on){juce::Path check;check.startNewSubPath(box.getX()+4,box.getCentreY());check.lineTo(box.getCentreX()-1,box.getBottom()-5);check.lineTo(box.getRight()-4,box.getY()+5);g.setColour(juce::Colour(0xff17121f));g.strokePath(check,juce::PathStrokeType(2.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));}
  g.setColour(over?juce::Colour(ink):juce::Colour(0xffd7d2e4));g.setFont(juce::Font(juce::FontOptions(12.f)));
  g.drawText(button.getButtonText(),r.withTrimmedLeft(6),juce::Justification::centredLeft);
 }
 void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float sliderPos,float,float,const juce::Slider::SliderStyle style,juce::Slider& slider)override{
  if(style!=juce::Slider::LinearHorizontal){juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,sliderPos,0,0,style,slider);return;}
  auto r=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height).reduced(0,4.f);
  const float cy=r.getCentreY();const float left=r.getX()+7,right=r.getRight()-7;
  g.setColour(juce::Colour(0xff2a2636));g.fillRoundedRectangle(left,cy-2,right-left,4.f,2.f);
  const float fill=slider.valueToProportionOfLength(slider.getValue());
  g.setColour(juce::Colour(accent));g.fillRoundedRectangle(left,cy-2,juce::jmax(0.f,(right-left)*fill),4.f,2.f);
  const float px=left+(right-left)*fill;
  g.setColour(juce::Colour(0xff14121b));g.fillEllipse(px-7,cy-7,14,14);
  g.setColour(juce::Colour(ink));g.drawEllipse(px-7,cy-7,14,14,1.5f);
  g.setColour(juce::Colour(accent));g.fillEllipse(px-3,cy-3,6,6);
 }
 void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,float position,float,float,juce::Slider& slider)override{
  auto area=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height).reduced(4);float radius=juce::jmin(area.getWidth(),area.getHeight())*.5f;auto center=area.getCentre();
  const float start=juce::MathConstants<float>::pi*1.18f,sweep=juce::MathConstants<float>::pi*1.64f;
  // outer inactive track
  g.setColour(juce::Colour(0xff272332));g.drawEllipse(center.x-radius*.97f,center.y-radius*.97f,radius*1.94f,radius*1.94f,2.f);
  const int ticks=72;
  for(int i=0;i<ticks;++i){const float t=(float)i/(ticks-1),angle=start+sweep*t;const bool lit=t<=position;
   const float r0=radius*(lit?.82f:.84f),r1=radius*(lit?.94f:.90f);
   g.setColour(lit?juce::Colour(accent).withAlpha(.55f+.45f*t):juce::Colour(0xff3a3548));
   g.drawLine(center.x+std::sin(angle)*r0,center.y-std::cos(angle)*r0,center.x+std::sin(angle)*r1,center.y-std::cos(angle)*r1,lit?2.f:1.f);}
  juce::ColourGradient body(juce::Colour(0xff2a2634),center.x-radius*.2f,center.y-radius*.45f,juce::Colour(0xff0f0e14),center.x+radius*.5f,center.y+radius*.6f,false);
  g.setGradientFill(body);g.fillEllipse(center.x-radius*.78f,center.y-radius*.78f,radius*1.56f,radius*1.56f);
  g.setColour(juce::Colour(0xff08080c));g.drawEllipse(center.x-radius*.78f,center.y-radius*.78f,radius*1.56f,radius*1.56f,1.5f);
  g.setColour(juce::Colour(0xff393244).withAlpha(.6f));g.drawEllipse(center.x-radius*.62f,center.y-radius*.62f,radius*1.24f,radius*1.24f,1.f);
  g.setColour(juce::Colour(ink));g.setFont(juce::Font(juce::FontOptions(11.f)));
  g.drawText(slider.getName(),juce::Rectangle<float>(center.x-radius,center.y+4,radius*2,16),juce::Justification::centred);
 }
 juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider)override{if(!slider.isRotary())return juce::LookAndFeel_V4::getSliderLayout(slider);juce::Slider::SliderLayout layout;layout.sliderBounds=slider.getLocalBounds();layout.textBoxBounds=juce::Rectangle<int>(slider.getWidth()/2-42,slider.getHeight()/2-20,84,26);return layout;}
};
class KeyButton final:public juce::TextButton {
public:
 bool blackKey=false,scaleDot=false,targetFill=false,inputOutline=false,selectedKey=false;
 void paintButton(juce::Graphics& g,bool over,bool down)override{
  auto r=getLocalBounds().toFloat().reduced(1.f,0.5f);const float radius=blackKey?5.f:6.f;
  juce::Colour top=blackKey?juce::Colour(0xff1a1822):juce::Colour(0xfff0edf7);
  juce::Colour bottom=blackKey?juce::Colour(0xff0d0c12):juce::Colour(0xffd7d2e3);
  if(over){top=top.brighter(.05f);bottom=bottom.brighter(.04f);}if(down){top=top.darker(.05f);bottom=bottom.darker(.05f);}
  g.setGradientFill(juce::ColourGradient(top,r.getTopLeft(),bottom,r.getBottomLeft(),false));
  g.fillRoundedRectangle(r,radius);
  if(targetFill){g.setColour(juce::Colour(0xff8b5cf6).withAlpha(blackKey?.92f:.78f));g.fillRoundedRectangle(r.reduced(1.f),radius);}
  g.setColour(blackKey?juce::Colour(0xff332d42):juce::Colour(0xffbdb6ce));g.drawRoundedRectangle(r,radius,1.f);
  const bool fill=targetFill;
  g.setColour(blackKey?(fill?juce::Colour(0xfff7f4ff):juce::Colour(0xffd7d2e4)):(fill?juce::Colour(0xfffbf9ff):juce::Colour(0xff443d55)));
  g.setFont(juce::Font(juce::FontOptions(blackKey?9.5f:11.f)));
  auto label=r.removeFromBottom(blackKey?22.f:28.f);
  g.drawText(getButtonText(),label,juce::Justification::centred);
  if(scaleDot){g.setColour(fill?juce::Colour(0xfff7f4ff):(blackKey?juce::Colour(0xffb9a6ff):juce::Colour(0xff7c5cff)));g.fillEllipse(r.getCentreX()-3.f,r.getBottom()-9.f,6.f,6.f);}
  if(inputOutline){g.setColour(juce::Colour(0xff8ee7ff));g.drawRoundedRectangle(r.reduced(2.f),radius-2.f,2.f);}
  if(selectedKey){g.setColour(juce::Colours::white);g.drawRoundedRectangle(r.reduced(4.f),radius-3.f,2.5f);g.fillRect(r.getCentreX()-6.f,r.getY()+3.f,12.f,3.f);}
 }
};
static juce::String megaTuneNoteName(float midi){
 static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
 if(midi<=0)return "--";const int note=(int)std::lround(midi);return juce::String(names[((note%12)+12)%12])+juce::String(note/12-1);
}
static juce::String megaTunePitchName(int pc){
 static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
 return names[((pc%12)+12)%12];
}
struct MegaTuneScale{const char* name;int mask;};
// Masks are relative to the key: bit 0 is the root.
static const MegaTuneScale megaTuneScales[]={{"Major",2741},{"Minor",1453},{"Harmonic Minor",2477},{"Melodic Minor",2733},{"Dorian",1709},{"Phrygian",1451},{"Lydian",2773},{"Mixolydian",1717},{"Major Pentatonic",661},{"Minor Pentatonic",1193},{"Blues",1257},{"Whole Tone",1365},{"Chromatic",4095}};
static juce::String megaTuneScaleName(int mask){for(auto& s:megaTuneScales)if(s.mask==mask)return s.name;return "Custom";}
static int megaTuneScaleIndex(int mask){int i=0;for(auto& s:megaTuneScales){if(s.mask==mask)return i;++i;}return -1;}
class HistoryRow final:public juce::Button {
public:
 explicit HistoryRow(int number):Button("History row"){index=number;setComponentID("history-row-"+juce::String(number));setWantsKeyboardFocus(true);}
 int index=0;float input=0,target=0;int reason=0;bool selected=false;
 void paintButton(juce::Graphics& g,bool over,bool down)override{
  auto r=getLocalBounds().toFloat();
  if(selected||over||down){g.setColour(selected?juce::Colour(0xff372a5e):juce::Colour(0xff211e2c));g.fillRoundedRectangle(r,7.f);}
  if(selected){g.setColour(juce::Colour(0xffb592ff));g.fillRoundedRectangle(r.withWidth(4.f),3.f);}
  static const char* rn[]={"Nearest enabled note","Held by stabilizer","Outside Range","Unvoiced","No enabled note"};
  g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(19.f,juce::Font::bold));
  g.drawText(input>0?megaTuneNoteName(input):juce::String("--"),r.withTrimmedLeft(16).withWidth(120),juce::Justification::centredLeft);
  g.setFont(juce::Font(16.f));g.setColour(juce::Colour(0xffb9b2c8));
  g.drawText(juce::String::fromUTF8("→"),r.withTrimmedLeft(138).withWidth(30),juce::Justification::centred);
  g.setFont(juce::Font(19.f,juce::Font::bold));g.setColour(juce::Colour(0xfff4f1ff));
  g.drawText(target>0?megaTuneNoteName(target):juce::String("--"),r.withTrimmedLeft(170).withWidth(120),juce::Justification::centredLeft);
  g.setFont(juce::Font(15.f));g.setColour(juce::Colour(0xffd7d0e4));
  g.drawText(rn[juce::jlimit(0,4,reason)],r.withTrimmedLeft(310).withTrimmedRight(8),juce::Justification::centredLeft);
  if(!selected){g.setColour(juce::Colour(0xff2c2739));g.fillRect(juce::Rectangle<float>(r.getX()+16,r.getBottom()-1,r.getWidth()-32,1));}
 }
};
class HistoryPanel final:public juce::Component {
public:
 HistoryPanel(){
  setComponentID("history-panel");setOpaque(true);
  for(int i=0;i<6;++i){rows[(size_t)i]=std::make_unique<HistoryRow>(i);addChildComponent(*rows[(size_t)i]);rows[(size_t)i]->onClick=[this,i]{if(onSelect)onSelect(i);};}
  addAndMakeVisible(close);close.setComponentID("history-close");close.setButtonText("X");close.onClick=[this]{if(onClose)onClose();};
  addAndMakeVisible(enable);enable.setComponentID("history-enable");enable.onClick=[this]{if(onEnable&&actionPc>=0)onEnable(actionPc);};
  addAndMakeVisible(exclude);exclude.setComponentID("history-exclude");exclude.onClick=[this]{if(onExclude&&actionPc>=0)onExclude(actionPc);};
  addAndMakeVisible(undo);undo.setComponentID("history-undo");undo.setButtonText("Undo");undo.onClick=[this]{if(onUndo)onUndo();};
  addChildComponent(status);status.setComponentID("history-status");status.setVisible(false);
 }
 void refresh(const MegaTune& proc,int selectedSeq){
  const int seq=proc.historySeq.load();actionPc=-1;actionEnabled=false;
  for(int i=0;i<6;++i){auto& row=*rows[(size_t)i];const int s=seq-1-i;
   megatune::DecisionView dv;if(s>=0&&s<seq&&proc.readDecision(s,dv)){row.input=dv.in;row.target=dv.target;row.reason=dv.reason;row.selected=s==selectedSeq;row.setVisible(true);}
   else{row.selected=false;row.setVisible(false);}
   row.repaint();}
  megatune::DecisionView sd;if(selectedSeq>=0&&selectedSeq<seq&&proc.readDecision(selectedSeq,sd)){if(sd.target>0){actionPc=(((int)std::lround(sd.target))%12+12)%12;const int root=(int)proc.value("key");const int degree=((actionPc-root)%12+12)%12;actionEnabled=proc.noteValue(degree)>.5f;}}
  actionName=actionPc>=0?megaTunePitchName(actionPc):juce::String();
  const int nearest=proc.currentTargetNote.load();nearestName=nearest>0?megaTuneNoteName((float)nearest):juce::String();
  enable.setButtonText(actionPc>=0?"Enable "+actionName:juce::String("Enable"));
  exclude.setButtonText(actionPc>=0?"Exclude "+actionName:juce::String("Exclude"));
  enable.setEnabled(actionPc>=0&&!actionEnabled);exclude.setEnabled(actionPc>=0&&actionEnabled);
  enable.setVisible(actionPc>=0&&!actionEnabled);exclude.setVisible(actionPc>=0&&actionEnabled);
  repaint();
 }
 void resized()override{
  close.setBounds(getWidth()-58,16,40,40);
  for(int i=0;i<6;++i)rows[(size_t)i]->setBounds(16,138+i*58,getWidth()-32,54);
  const int actionY=660;enable.setBounds(24,actionY,190,52);exclude.setBounds(24,actionY,190,52);
  undo.setBounds(getWidth()-116,getHeight()-72,88,44);
 }
 void paint(juce::Graphics& g)override{
  juce::ColourGradient bg(juce::Colour(0xff191622),0,0,juce::Colour(0xff0f0d15),0,(float)getHeight(),false);
  g.setGradientFill(bg);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),14.f);
  g.setColour(juce::Colour(0xff332c44));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),14.f,1.f);
  g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(30.f,juce::Font::bold));g.drawText("Why this note?",24,18,420,40,juce::Justification::left);
  g.setColour(juce::Colour(0xff9a94a8));g.setFont(juce::Font(18.f));g.drawText("Recent pitch decisions",24,60,420,26,juce::Justification::left);
  g.setColour(juce::Colour(0xff8d879d));g.setFont(juce::Font(13.f));
  g.drawText("INPUT",24,108,120,18,juce::Justification::left);
  g.drawText("TARGET",178,108,120,18,juce::Justification::left);
  g.drawText("REASON",318,108,200,18,juce::Justification::left);
  g.setColour(juce::Colour(0xff2d2939));g.fillRect(24,132,getWidth()-48,1);
  const bool has=actionPc>=0;const juce::String title=has?(actionName+(actionEnabled?" is enabled":" is excluded")):"No target";
  g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(27.f,juce::Font::bold));g.drawText(title,24,548,getWidth()-48,40,juce::Justification::left);
  juce::String body;
  if(!has)body="This decision did not select a note.";
  else if(!actionEnabled)body="The nearest enabled note was "+(nearestName.isNotEmpty()?nearestName:juce::String("none"))+".";
  else body="This note is enabled in the current scale.";
  g.setColour(juce::Colour(0xffd7d0e4));g.setFont(juce::Font(17.f));g.drawFittedText(body,24,596,getWidth()-48,52,juce::Justification::topLeft,2);
  g.setColour(juce::Colour(0xff2d2939));g.fillRect(24,636,getWidth()-48,1);
  juce::Rectangle<float> bar(16,(float)getHeight()-88.f,(float)getWidth()-32.f,64.f);
  g.setColour(juce::Colour(0xff201c2b));g.fillRoundedRectangle(bar,14.f);g.setColour(juce::Colour(0xff383047));g.drawRoundedRectangle(bar,14.f,1.f);
  g.setColour(juce::Colour(0xffd7d0e4));g.setFont(juce::Font(17.f));
  g.drawText(has?(actionName+(actionEnabled?" enabled":" excluded")):"No target",40,bar.getY()+18,getWidth()-190,28,juce::Justification::left);
 }
 int actionPc=-1;bool actionEnabled=false;juce::String actionName,nearestName;
 std::function<void(int)> onSelect,onEnable,onExclude;std::function<void()> onClose,onUndo;
private:
 std::array<std::unique_ptr<HistoryRow>,6> rows;
 juce::TextButton close{"X"},enable{"Enable"},exclude{"Exclude"},undo{"Undo"};juce::Label status;
};

struct PresetEntry{juce::String name,category,about;int factory=-1;};
class PresetRow final:public juce::Button {
public:
 PresetRow():Button("Preset row"){}
 PresetEntry entry;bool selected=false;std::function<void()> onDouble;
 void mouseDoubleClick(const juce::MouseEvent&)override{if(onDouble)onDouble();}
 void paintButton(juce::Graphics& g,bool over,bool)override{
  auto r=getLocalBounds().toFloat();
  if(selected||over){g.setColour(selected?juce::Colour(0xff8b5cf6):juce::Colour(0xff221f2d));g.fillRoundedRectangle(r.reduced(0,1),7);}
  g.setColour(selected?juce::Colour(0xff17121f):juce::Colour(0xfff4f1ff));g.setFont(juce::Font(16.f,selected?juce::Font::bold:juce::Font::plain));
  g.drawText(entry.name,r.withTrimmedLeft(14).withTrimmedRight(96),juce::Justification::centredLeft);
  g.setColour(selected?juce::Colour(0xff2b1d4d):juce::Colour(0xff8d879d));g.setFont(juce::Font(12.f));
  g.drawText(entry.category,r.withTrimmedRight(14),juce::Justification::centredRight);
  if(!selected){g.setColour(juce::Colour(0xff2d2939));g.fillRect(juce::Rectangle<float>(r.getX()+8,r.getBottom()-1,r.getWidth()-16,1));}
 }
};
// Clicking a row applies it straight away so it can be heard; Done keeps it, Cancel
// (or Esc) puts the previous sound back.
class PresetBrowser final:public juce::Component {
public:
 static constexpr int pw=960,ph=620,numCategories=9;
 static const char* categoryName(int i){static const char* names[]={"All","Natural","Pop","Hip-Hop","R&B","Rock","Doubles","Effect","User"};return names[juce::jlimit(0,numCategories-1,i)];}
 PresetBrowser(MegaTune& p):proc(p){
  setComponentID("preset-browser");setOpaque(false);setInterceptsMouseClicks(true,true);setWantsKeyboardFocus(true);
  auto styleEditor=[](juce::TextEditor& e,const char* hint){e.setTextToShowWhenEmpty(hint,juce::Colour(0xff8d879d));
   e.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff17151f));e.setColour(juce::TextEditor::textColourId,juce::Colour(0xfff4f1ff));
   e.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff383047));e.setColour(juce::TextEditor::focusedOutlineColourId,juce::Colour(0xffa78bfa));e.setFont(juce::Font(15.f));e.setJustification(juce::Justification::centredLeft);e.setIndents(12,0);};
  addAndMakeVisible(search);styleEditor(search,"Search presets");search.onTextChange=[this]{refreshRows();};
  addAndMakeVisible(saveName);styleEditor(saveName,"Name your sound");saveName.onReturnKey=[this]{saveCurrent();};
  addAndMakeVisible(list);list.setScrollBarThickness(8);list.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff5a4b7a));
  for(int i=0;i<numCategories;++i){auto* chip=chips.add(new juce::TextButton(categoryName(i)));addAndMakeVisible(chip);chip->onClick=[this,i]{category=i;refreshRows();repaint();};}
  addAndMakeVisible(close);close.onClick=[this]{cancelAndHide();};
  addAndMakeVisible(cancel);cancel.onClick=[this]{cancelAndHide();};
  addAndMakeVisible(load);load.setComponentID("preset-load");load.onClick=[this]{setVisible(false);if(onDone)onDone();};
  addAndMakeVisible(save);save.setTooltip("Save the current sound as a user preset");save.onClick=[this]{saveCurrent();};
  addChildComponent(remove);remove.onClick=[this]{if(selFactory>=0||selUser.isEmpty())return;proc.deleteUserPreset(selUser);selUser.clear();selFactory=proc.getCurrentProgram();rebuild();repaint();};
  addAndMakeVisible(keep);keep.setTooltip("On: presets change the sound only. Off: presets also set their own key and scale.");
  keep.setToggleState(proc.keepKey.load(),juce::dontSendNotification);keep.onClick=[this]{proc.keepKey=keep.getToggleState();repaint();};
  list.setViewedComponent(&listContent,false);
 }
 ~PresetBrowser()override{list.setViewedComponent(nullptr,false);}
 void show(){const auto user=proc.getUserPresetName();selUser=user;selFactory=user.isEmpty()?proc.getCurrentProgram():-1;category=0;search.clear();saveName.clear();
  keep.setToggleState(proc.keepKey.load(),juce::dontSendNotification);rebuild();setVisible(true);toFront(true);grabKeyboardFocus();}
 void cancelAndHide(){setVisible(false);if(onCancel)onCancel();}
 void saveCurrent(){const auto name=saveName.getText().trim();if(name.isEmpty()){saveName.grabKeyboardFocus();return;}
  if(!proc.saveUserPreset(name))return;selUser=proc.getUserPresetName();selFactory=-1;category=numCategories-1;saveName.clear();rebuild();repaint();}
 void rebuild(){entries.clear();const auto* factory=MegaTune::factoryPresets();
  for(int i=0;i<MegaTune::numFactoryPresets;++i)entries.push_back({factory[i].name,factory[i].category,factory[i].about,i});
  for(auto& name:MegaTune::userPresetNames())entries.push_back({name,"User","Your saved sound.",-1});
  refreshRows();}
 bool isSelected(const PresetEntry& e)const{return e.factory>=0?e.factory==selFactory:(selFactory<0&&e.name==selUser);}
 const PresetEntry* selected()const{for(auto& e:entries)if(isSelected(e))return &e;return nullptr;}
 // Rows call this from their own click handler, so it must not rebuild the rows.
 void pick(const PresetEntry& e){selFactory=e.factory;selUser=e.factory<0?e.name:juce::String();if(onPick)onPick(e);refreshSelection();repaint();}
 void refreshSelection(){for(auto* row:rows){row->selected=isSelected(row->entry);row->repaint();}remove.setVisible(selFactory<0&&selUser.isNotEmpty());}
 void refreshRows(){
  const auto text=search.getText().trim().toLowerCase();const int w=juce::jmax(1,list.getWidth()-10);int y=0;
  list.setViewedComponent(nullptr,false);rows.clear();
  for(auto& e:entries){
   const bool inCategory=category==0||(category==numCategories-1?e.factory<0:(e.factory>=0&&e.category==categoryName(category)));
   if(!inCategory||(text.isNotEmpty()&&!e.name.toLowerCase().contains(text)&&!e.category.toLowerCase().contains(text)))continue;
   auto* row=rows.add(new PresetRow());row->entry=e;row->setBounds(0,y,w,42);const PresetEntry copy=e;
   row->onClick=[this,copy]{pick(copy);};row->onDouble=[this]{load.onClick();};listContent.addAndMakeVisible(row);y+=44;}
  listContent.setSize(w,juce::jmax(1,y));list.setViewedComponent(&listContent,false);
  for(int i=0;i<chips.size();++i)chips[i]->setToggleState(i==category,juce::dontSendNotification);
  refreshSelection();
 }
 bool keyPressed(const juce::KeyPress& press)override{
  if(press==juce::KeyPress::escapeKey){cancelAndHide();return true;}
  if(press==juce::KeyPress::returnKey){load.onClick();return true;}
  if(press==juce::KeyPress::upKey||press==juce::KeyPress::downKey){int at=-1;for(int i=0;i<rows.size();++i)if(rows[i]->selected)at=i;
   const int next=juce::jlimit(0,juce::jmax(0,rows.size()-1),at+(press==juce::KeyPress::downKey?1:-1));
   if(rows.size()>0&&next!=at){const PresetEntry e=rows[next]->entry;pick(e);list.setViewPosition(0,juce::jmax(0,next*44-132));}return true;}
  return false;}
 void resized()override{
  const int px=(getWidth()-pw)/2,py=(getHeight()-ph)/2;
  close.setBounds(px+pw-58,py+18,40,40);
  for(int i=0;i<chips.size();++i)chips[i]->setBounds(px+32+i*100,py+96,94,34);
  search.setBounds(px+32,py+146,440,40);list.setBounds(px+32,py+198,440,326);
  keep.setBounds(px+32,py+ph-62,216,40);saveName.setBounds(px+262,py+ph-62,190,40);save.setBounds(px+458,py+ph-62,104,40);remove.setBounds(px+568,py+ph-62,84,40);
  cancel.setBounds(px+694,py+ph-62,106,40);load.setBounds(px+808,py+ph-62,120,40);
  refreshRows();
 }
 void paint(juce::Graphics& g)override{
  const auto ink=juce::Colour(0xfff4f1ff),muted=juce::Colour(0xff9a94a8),line=juce::Colour(0xff2d2939),accent=juce::Colour(0xffa78bfa);
  g.fillAll(juce::Colours::black.withAlpha(.62f));
  const int px=(getWidth()-pw)/2,py=(getHeight()-ph)/2;
  juce::Rectangle<float> panel((float)px,(float)py,(float)pw,(float)ph);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b1825),panel.getTopLeft(),juce::Colour(0xff0f0d15),panel.getBottomLeft(),false));g.fillRoundedRectangle(panel,16);
  g.setColour(juce::Colour(0xff3a324c));g.drawRoundedRectangle(panel,16,1);
  g.setColour(ink);g.setFont(juce::Font(28.f,juce::Font::bold));g.drawText("Presets",px+32,py+24,130,38,juce::Justification::left);
  int users=0;for(auto& e:entries)if(e.factory<0)++users;
  g.setColour(muted);g.setFont(juce::Font(15.f));g.drawText(juce::String(MegaTune::numFactoryPresets)+" factory, "+juce::String(users)+" yours",px+150,py+32,300,24,juce::Justification::left);
  g.setColour(line);g.fillRect(px+32,py+80,pw-64,1);g.fillRect(px+496,py+146,1,378);g.fillRect(px+32,py+ph-76,pw-64,1);
  if(rows.isEmpty()){g.setColour(muted);g.setFont(juce::Font(15.f));
   g.drawFittedText(category==numCategories-1&&search.isEmpty()?"No saved sounds yet. Name one below and press Save.":"Nothing matches.",px+32,py+214,440,44,juce::Justification::centredTop,2);}
  const int rx=px+528,rw=400;
  if(const auto* e=selected()){
   g.setColour(ink);g.setFont(juce::Font(27.f,juce::Font::bold));g.drawText(e->name,rx,py+146,rw,36,juce::Justification::left);
   g.setColour(accent);g.setFont(juce::Font(13.f,juce::Font::bold));g.drawText(e->factory>=0?(e->category.toUpperCase()+"  /  FACTORY"):juce::String("USER PRESET"),rx,py+184,rw,18,juce::Justification::left);
   g.setColour(juce::Colour(0xffd7d0e4));g.setFont(juce::Font(15.f));g.drawFittedText(e->about,rx,py+210,rw,42,juce::Justification::topLeft,2);
   if(e->factory>=0){const auto& p=MegaTune::factoryPresets()[e->factory];int y=py+270;
    auto bar=[&](const char* label,float fraction,const juce::String& value,bool bipolar){
     g.setColour(juce::Colour(0xffd7d0e4));g.setFont(juce::Font(14.f));g.drawText(label,rx,y,120,20,juce::Justification::left);
     const float tx=(float)rx+124,tw=190,ty=(float)y+7;g.setColour(juce::Colour(0xff2a2636));g.fillRoundedRectangle(tx,ty,tw,6,3);
     g.setColour(accent);const float f=juce::jlimit(0.f,1.f,fraction);
     if(bipolar){const float a=juce::jmin(.5f,f),b=juce::jmax(.5f,f);g.fillRoundedRectangle(tx+tw*a,ty,juce::jmax(2.f,tw*(b-a)),6,3);}
     else g.fillRoundedRectangle(tx,ty,juce::jmax(2.f,tw*f),6,3);
     g.setColour(ink);g.setFont(juce::Font(14.f,juce::Font::bold));g.drawText(value,rx+rw-84,y,84,20,juce::Justification::right);y+=30;};
    bar("Speed",megaTuneSpeedToNormalized(p.speed),p.speed<0?juce::String("Hard"):juce::String((int)p.speed)+" ms",false);
    bar("Sustain",(p.sustain+100.f)/200.f,(p.sustain>0?"+":"")+juce::String((int)p.sustain)+"%",true);
    bar("Amount",p.amount/100.f,juce::String((int)p.amount)+"%",false);
    bar("Doubler",p.doubler/100.f,juce::String((int)p.doubler)+"%",false);
    bar("Width",p.width/100.f,juce::String((int)p.width)+"%",false);
    static const char* keys[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    g.setColour(muted);g.setFont(juce::Font(13.f));
    g.drawText(juce::String("Note Stabilizer ")+(p.stabilize?"on":"off")+".  "+(keep.getToggleState()?juce::String("Keeps your key and scale."):"Sets "+juce::String(keys[juce::jlimit(0,11,p.key)])+" "+megaTuneScaleName(p.mask)+"."),rx,y+4,rw,20,juce::Justification::left);}
   else{g.setColour(muted);g.setFont(juce::Font(13.f));g.drawFittedText("Saved in ~/Library/Application Support/ademusic/MegaTune/Presets and available in every project.",rx,py+270,rw,40,juce::Justification::topLeft,2);}
  }
  g.setColour(muted);g.setFont(juce::Font(12.f));g.drawText("Click to audition.  Done keeps it, Cancel restores your sound.",rx,py+ph-106,rw,18,juce::Justification::left);
 }
 std::function<void(const PresetEntry&)> onPick;std::function<void()> onDone,onCancel;
private:
 MegaTune& proc;std::vector<PresetEntry> entries;int category=0,selFactory=0;juce::String selUser;
 juce::TextButton close{"X"},cancel{"Cancel"},load{"Done"},save{"Save"},remove{"Delete"};juce::ToggleButton keep{"Keep my key and scale"};
 juce::TextEditor search,saveName;juce::OwnedArray<juce::TextButton> chips;
 juce::Component listContent;juce::Viewport list;juce::OwnedArray<PresetRow> rows;
};
class Canvas final:public juce::Component {
public:
 std::function<void(juce::Graphics&)> onPaint;std::function<void()> onResized;
 void paint(juce::Graphics& g)override{if(onPaint)onPaint(g);}
 void resized()override{if(onResized)onResized();}
};
class Editor final:public juce::AudioProcessorEditor,private juce::Timer {
 TunerLook tunerLook;Canvas canvas;MegaTune& proc;juce::Slider speed,sustain,amount,doubler,width,reference,shift,range;juce::ComboBox presets;juce::ToggleButton bypass{"Global Bypass"},stabilize{"Note Stabilizer"},lowLatency{"Low Latency"};juce::ComboBox key;int displayedKey=-1;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment;juce::TextButton major{"Major"},minor{"Minor"},chromatic{"Chromatic"};
 struct Sound {std::vector<float> values;int program=0;juce::String user;};
 juce::TextButton previous{"<"},next{">"},undo{"Undo"},redo{"Redo"},slotA{"A"},slotB{"B"},copyAB{"A > B"};juce::TextButton why{"Why this note?"},browse{"Browse"};bool showWhy=false;int selSeq=-1,selectedPc=-1,shownProgram=-1;HistoryPanel historyPanel;PresetBrowser presetBrowser;juce::Label experimental;juce::ComboBox enginePick,scalePick;int shownEngine=-1,shownMask=-1;juce::String shownUser;juce::TooltipWindow tips{this,600};Sound beforeBrowse;bool browsing=false;
 static constexpr int graphN=200;std::array<float,graphN> inTrace{},outTrace{};int graphHead=0;float graphCentre=60.f,inMeter=0,outMeter=0;bool graphEver=false;
 Sound comparison[2],observed;std::vector<Sound> past,future;int activeSlot=0;
 float animationTime=0,smoothedLevel=0;
 Sound capture()const{Sound sound;sound.program=proc.getCurrentProgram();sound.user=proc.getUserPresetName();for(auto* p:proc.getParameters())sound.values.push_back(p->getValue());return sound;}
 bool same(const Sound& a,const Sound& b)const{return a.program==b.program&&a.user==b.user&&a.values==b.values;}
 void restore(const Sound& sound){proc.currentProgram=sound.program;proc.setUserPresetName(sound.user);auto& parameters=proc.getParameters();for(size_t i=0;i<sound.values.size()&&i<(size_t)parameters.size();++i){parameters[(int)i]->beginChangeGesture();parameters[(int)i]->setValueNotifyingHost(sound.values[i]);parameters[(int)i]->endChangeGesture();}observed=capture();canvas.repaint();}
 void remember(){auto now=capture();if(!same(now,observed)){past.push_back(observed);if(past.size()>100)past.erase(past.begin());future.clear();observed=now;}}
 void edit(const std::function<void()>& action){remember();auto before=capture();action();auto after=capture();if(!same(before,after)){past.push_back(before);future.clear();}observed=after;}
 void compare(int slot){comparison[activeSlot]=capture();activeSlot=slot;restore(comparison[slot]);past.clear();future.clear();}
 using SA=juce::AudioProcessorValueTreeState::SliderAttachment;using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
 std::unique_ptr<SA> a,b,c,da,wa,ra,sa,rga;std::unique_ptr<BA> d,e,ll;std::array<KeyButton,12> notes;std::array<std::unique_ptr<BA>,12> na;
 void syncKey(){const int root=(int)proc.value("key");if(root!=displayedKey){displayedKey=root;for(int i=0;i<12;++i){na[i].reset();na[i]=std::make_unique<BA>(proc.state,"note"+juce::String((i-root+12)%12),notes[i]);}}}
 void setSelectedPc(int pc){if(pc==selectedPc)return;selectedPc=pc;for(auto& note:notes)note.repaint();}
 void refreshHistory(){const int seq=proc.historySeq.load();if(selSeq>=seq)selSeq=seq-1;if(selSeq<0&&seq>0)selSeq=seq-1;if(selSeq>=0&&seq-selSeq>16)selSeq=seq-1;historyPanel.refresh(proc,selSeq);setSelectedPc(historyPanel.actionPc);}
 void openHistory(){if(showWhy)return;showWhy=true;selSeq=proc.historySeq.load()-1;why.setButtonText("Close");why.setVisible(false);resized();
  refreshHistory();historyPanel.setVisible(true);historyPanel.toFront(false);historyPanel.grabKeyboardFocus();canvas.repaint();}
 void closeHistory(){if(!showWhy)return;showWhy=false;selSeq=-1;setSelectedPc(-1);historyPanel.setVisible(false);
  why.setButtonText("Why this note?");why.setVisible(true);resized();why.grabKeyboardFocus();canvas.repaint();}
 void setTargetEnabled(int pc,bool enable){const int root=(int)proc.value("key");const int degree=((pc-root)%12+12)%12;
  auto* parameter=proc.state.getParameter("note"+juce::String(degree));if(parameter==nullptr)return;
  if((parameter->getValue()>.5f)==enable)return;
  edit([&]{parameter->beginChangeGesture();parameter->setValueNotifyingHost(enable?1.f:0.f);parameter->endChangeGesture();});
  refreshHistory();canvas.repaint();}
 void timerCallback()override{animationTime+=1.f/30.f;smoothedLevel+=(juce::jlimit(0.f,1.f,proc.level.load()*2)-smoothedLevel)*.18f;
 const float detected=proc.pitch.load();const int target=megatune::currentTargetNote(proc);
 {const bool bypassed=proc.value("bypass")>.5f;inTrace[(size_t)graphHead]=detected;outTrace[(size_t)graphHead]=bypassed?detected:proc.outputPitch.load();graphHead=(graphHead+1)%graphN;
  if(detected>0){if(!graphEver||std::abs(detected-graphCentre)>18.f)graphCentre=detected;else graphCentre+=(detected-graphCentre)*.05f;graphEver=true;}
  auto meterValue=[](float peak){return juce::jlimit(0.f,1.f,(juce::Decibels::gainToDecibels(peak,-60.f)+60.f)/60.f);};
  const float li=meterValue(proc.level.load()),lo=meterValue(proc.outputLevel.load());inMeter=li>inMeter?li:inMeter+(li-inMeter)*.2f;outMeter=lo>outMeter?lo:outMeter+(lo-outMeter)*.2f;}
 const int ip=detected>0?(((int)std::lround(detected))%12+12)%12:-1;
 const int op=target>0?((target%12)+12)%12:-1;const int root=(int)proc.value("key");
 for(int i=0;i<12;++i){notes[i].scaleDot=proc.noteValue(((i-root)+24)%12)>.5f;notes[i].targetFill=i==op;notes[i].inputOutline=i==ip;notes[i].selectedKey=i==selectedPc;notes[i].repaint();}
 if(!browsing&&!juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())remember();
 if(showWhy)refreshHistory();
 const int wantEngine=proc.value("smith")>.5f?3:proc.value("lowlatency")>.5f?2:1;
 if(wantEngine!=shownEngine){shownEngine=wantEngine;enginePick.setSelectedId(wantEngine,juce::dontSendNotification);}
 experimental.setVisible(false);
 undo.setEnabled(!past.empty());redo.setEnabled(!future.empty());slotA.setToggleState(activeSlot==0,juce::dontSendNotification);slotB.setToggleState(activeSlot==1,juce::dontSendNotification);
 const int program=proc.getCurrentProgram();const auto user=proc.getUserPresetName();
 if(program!=shownProgram||user!=shownUser){shownProgram=program;shownUser=user;if(user.isEmpty())presets.setSelectedId(program+1,juce::dontSendNotification);else presets.setText(user,juce::dontSendNotification);}
 {int mask=0;for(int n=0;n<12;++n)if(proc.noteValue(n)>.5f)mask|=1<<n;if(mask!=shownMask){shownMask=mask;const int index=megaTuneScaleIndex(mask);if(index>=0)scalePick.setSelectedId(index+1,juce::dontSendNotification);else scalePick.setText("Custom",juce::dontSendNotification);}}
 syncKey();canvas.repaint(31,135,(showWhy?844:1419),220);canvas.repaint(0,1028,1480,32);}
 void setScale(bool isMinor,bool all){const int scaleMajor[]={0,2,4,5,7,9,11};const int scaleMinor[]={0,2,3,5,7,8,10};for(int i=0;i<12;++i){bool selected=all;for(int j=0;j<7;++j)if(i==(isMinor?scaleMinor[j]:scaleMajor[j]))selected=true;auto* p=proc.state.getParameter("note"+juce::String(i));p->beginChangeGesture();p->setValueNotifyingHost(selected?1.f:0.f);p->endChangeGesture();}}
public:
 void tick(){timerCallback();}
 Editor(MegaTune& p):AudioProcessorEditor(p),proc(p),presetBrowser(p){setLookAndFeel(&tunerLook);addAndMakeVisible(canvas);canvas.onPaint=[this](juce::Graphics& g){paintCanvas(g);};canvas.onResized=[this]{layoutCanvas();};setSize(1120,802);auto setup=[&](juce::Slider& s,const char* id){addAndMakeVisible(s);s.setLookAndFeel(&tunerLook);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.setNumDecimalPlacesToDisplay(1);s.setDoubleClickReturnValue(true,proc.state.getParameter(id)->convertFrom0to1(proc.state.getParameter(id)->getDefaultValue()));s.setColour(juce::Slider::thumbColourId,juce::Colour(0xffbb8de9));s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,75,22);s.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffc48cea));s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff343137));s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);s.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xfff4f1ff));s.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);auto attachment=std::make_unique<SA>(proc.state,id,s);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.updateText();return attachment;};a=setup(speed,"speed");b=setup(sustain,"sustain");c=setup(amount,"amount");da=setup(doubler,"doubler");wa=setup(width,"width");ra=setup(reference,"reference");sa=setup(shift,"shift");rga=setup(range,"range");for(auto* slider:{&doubler,&width}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,38,20);slider->setTextValueSuffix("%");}reference.setSliderStyle(juce::Slider::LinearHorizontal);reference.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,20);reference.setTextValueSuffix(" Hz");reference.textFromValueFunction=[](double v){return juce::String(v,0);};reference.updateText();doubler.setName("Doubler");width.setName("Width");reference.setName("Reference");shift.setSliderStyle(juce::Slider::LinearHorizontal);shift.setTextBoxStyle(juce::Slider::TextBoxRight,false,46,20);shift.setTextValueSuffix(" st");shift.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};shift.updateText();shift.setName("Shift");range.setSliderStyle(juce::Slider::LinearHorizontal);range.setTextBoxStyle(juce::Slider::TextBoxRight,false,40,20);range.setTextValueSuffix(" st");range.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};range.updateText();range.setName("Range");
 addAndMakeVisible(presets);presets.setComponentID("preset-combo");for(int i=0;i<proc.getNumPrograms();++i)presets.addItem(proc.getProgramName(i),i+1);presets.setSelectedId(proc.getCurrentProgram()+1,juce::dontSendNotification);presets.onChange=[this]{if(presets.getSelectedId()>0)edit([&]{proc.setCurrentProgram(presets.getSelectedId()-1);});};
 addAndMakeVisible(browse);browse.setComponentID("preset-browse");browse.setButtonText("Presets");browse.onClick=[this]{remember();beforeBrowse=capture();browsing=true;presetBrowser.show();};
 addChildComponent(presetBrowser);presetBrowser.setVisible(false);presetBrowser.onPick=[this](const PresetEntry& entry){if(entry.factory>=0)proc.setCurrentProgram(entry.factory);else proc.loadUserPreset(entry.name);};
 presetBrowser.onDone=[this]{browsing=false;const auto now=capture();if(!same(beforeBrowse,now)){past.push_back(beforeBrowse);future.clear();}observed=now;canvas.repaint();};
 presetBrowser.onCancel=[this]{browsing=false;restore(beforeBrowse);canvas.repaint();};
 for(auto* button:{&previous,&next,&undo,&redo,&slotA,&slotB,&copyAB})addAndMakeVisible(button);
 previous.setTooltip("Previous factory preset");next.setTooltip("Next factory preset");copyAB.setTooltip("Copy the current sound to the other comparison slot");
 previous.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+proc.getNumPrograms()-1)%proc.getNumPrograms());});};next.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+1)%proc.getNumPrograms());});};
 undo.onClick=[this]{remember();if(past.empty())return;future.push_back(capture());auto sound=past.back();past.pop_back();restore(sound);};
 redo.onClick=[this]{if(future.empty())return;past.push_back(capture());auto sound=future.back();future.pop_back();restore(sound);};
 slotA.onClick=[this]{if(activeSlot!=0)compare(0);};slotB.onClick=[this]{if(activeSlot!=1)compare(1);};copyAB.onClick=[this]{comparison[1-activeSlot]=capture();};
 speed.setName("Speed");sustain.setName("Sustain");amount.setName("Amount");speed.textFromValueFunction=[](double v){return juce::String(v,1)+" ms";};sustain.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};amount.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};speed.updateText();sustain.updateText();amount.updateText();speed.setComponentID("control-speed");sustain.setComponentID("control-sustain");amount.setComponentID("control-amount");doubler.setComponentID("control-doubler");width.setComponentID("control-width");reference.setComponentID("control-reference");stabilize.setComponentID("control-stabilize");undo.setComponentID("undo-button");for(auto* v:{&bypass,&stabilize,&lowLatency})addAndMakeVisible(v);d=std::make_unique<BA>(proc.state,"bypass",bypass);e=std::make_unique<BA>(proc.state,"stabilize",stabilize);ll=std::make_unique<BA>(proc.state,"lowlatency",lowLatency);
 const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i){addAndMakeVisible(notes[i]);notes[i].setButtonText(names[i]);notes[i].setClickingTogglesState(true);bool black=i==1||i==3||i==6||i==8||i==10;notes[i].blackKey=black;na[i]=std::make_unique<BA>(proc.state,"note"+juce::String(i),notes[i]);}
 addAndMakeVisible(key);const char* roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i)key.addItem(roots[i],i+1);keyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state,"key",key);key.onChange=[this]{syncKey();canvas.repaint();};
 for(auto* v:{&major,&minor,&chromatic})addAndMakeVisible(v);major.onClick=[this]{setScale(false,false);};minor.onClick=[this]{setScale(true,false);};chromatic.onClick=[this]{setScale(false,true);};
 addAndMakeVisible(scalePick);scalePick.setComponentID("scale-pick");{int id=1;for(auto& s:megaTuneScales)scalePick.addItem(s.name,id++);}
 scalePick.onChange=[this]{const int id=scalePick.getSelectedId();if(id<1)return;const int mask=megaTuneScales[id-1].mask;edit([&]{for(int i=0;i<12;++i)proc.setParam("note"+juce::String(i),(mask&(1<<i))?1.f:0.f);});};
 speed.setTooltip("Retune time. Turn right for tighter tuning; fully right is hard tune. Double-click to reset.");
 sustain.setTooltip("How correction changes while a note is held. Left relaxes it and keeps vibrato; right tightens it.");
 amount.setTooltip("How much of the correction is applied. 100% lands exactly on the note.");
 doubler.setTooltip("Blends in two detuned, delayed copies, like a second take.");
 width.setTooltip("Spreads the doubled voices left and right. Sums back to mono cleanly.");
 reference.setTooltip("Tuning reference for A4. Leave at 440 Hz unless the song is tuned differently.");
 shift.setTooltip("Transposes the corrected voice in semitones.");
 range.setTooltip("Notes further than this from a scale note are left uncorrected.");
 stabilize.setTooltip("Holds the current note through small wobbles so the tuner does not flip between neighbours.");
 bypass.setTooltip("Passes the input through, delayed to stay in time.");
 enginePick.setTooltip("Quality has the cleanest sound. Low Latency is for tracking while recording.");
 key.setTooltip("The key of the song.");scalePick.setTooltip("Scale for the chosen key. Click keys below to add or remove single notes.");
 browse.setTooltip("Browse, audition and save presets");why.setTooltip("Shows the tuner's recent decisions and lets you exclude a note");
 undo.setTooltip("Undo (Cmd+Z)");redo.setTooltip("Redo (Shift+Cmd+Z)");slotA.setTooltip("Sound A");slotB.setTooltip("Sound B");
 setWantsKeyboardFocus(true);
 addAndMakeVisible(enginePick);addAndMakeVisible(why);addAndMakeVisible(historyPanel);historyPanel.setVisible(false);historyPanel.setWantsKeyboardFocus(true);
 historyPanel.onClose=[this]{closeHistory();};historyPanel.onSelect=[this](int row){selSeq=proc.historySeq.load()-1-row;refreshHistory();};
 historyPanel.onEnable=[this](int pc){setTargetEnabled(pc,true);};historyPanel.onExclude=[this](int pc){setTargetEnabled(pc,false);};historyPanel.onUndo=[this]{undo.onClick();};
 addAndMakeVisible(experimental);experimental.setComponentID("experimental-label");experimental.setText("EXPERIMENTAL",juce::dontSendNotification);experimental.setFont(10.f);experimental.setColour(juce::Label::textColourId,juce::Colour(0xff9a6bff));experimental.setVisible(false);experimental.setInterceptsMouseClicks(false,false);
 why.setComponentID("why-button");why.onClick=[this]{if(showWhy)closeHistory();else openHistory();};
 enginePick.addItem("Quality",1);enginePick.addItem("Low Latency",2);enginePick.addItem("Signalsmith",3);enginePick.setSelectedId(1,juce::dontSendNotification);enginePick.onChange=[this]{const int id=enginePick.getSelectedId();auto setP=[&](const char* pid,float v){auto* pr=proc.state.getParameter(pid);pr->beginChangeGesture();pr->setValueNotifyingHost(v);pr->endChangeGesture();};setP("lowlatency",id==2?1.f:0.f);setP("engine",1.f);setP("smith",id==3?1.f:0.f);};
 auto styleCombo=[&](juce::ComboBox& c){c.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff1b1826));c.setColour(juce::ComboBox::textColourId,juce::Colour(0xfff4f1ff));c.setColour(juce::ComboBox::arrowColourId,juce::Colour(0xff8d879d));c.setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff2d2939));};
 styleCombo(presets);styleCombo(key);styleCombo(enginePick);styleCombo(scalePick);
 // Move the complete UI tree onto the scalable canvas.
 for(int i=getNumChildComponents();--i>=0;){auto* child=getChildComponent(i);if(child!=&canvas)canvas.addChildComponent(child);}
 setResizeLimits(740,530,1480,1060);setResizable(true,true);if(auto* c=getConstrainer())c->setFixedAspectRatio(1480.0/1060.0);
 observed=capture();comparison[0]=comparison[1]=observed;resized();timerCallback();startTimerHz(30);}
 ~Editor()override{stopTimer();for(auto* slider:{&speed,&sustain,&amount,&doubler,&width,&reference,&shift,&range})slider->setLookAndFeel(nullptr);setLookAndFeel(nullptr);}
 void resized()override{
  const float scale=juce::jmin(getWidth()/1480.f,getHeight()/1060.f);
  const float x=(getWidth()-1480.f*scale)*.5f,y=(getHeight()-1060.f*scale)*.5f;
  canvas.setBounds(0,0,1480,1060);canvas.setTransform(juce::AffineTransform::scale(scale).translated(x,y));layoutCanvas();
 }
 void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff0b0c10));}
 bool keyPressed(const juce::KeyPress& press)override{
  if(press==juce::KeyPress('z',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier,0)){redo.onClick();return true;}
  if(press==juce::KeyPress('z',juce::ModifierKeys::commandModifier,0)){undo.onClick();return true;}
  return false;}
 void layoutCanvas(){
  presets.setBounds(266,20,330,44);browse.setBounds(602,20,70,44);previous.setBounds(680,20,44,44);next.setBounds(730,20,44,44);
  undo.setBounds(830,20,94,44);redo.setBounds(932,20,94,44);
  slotA.setBounds(1034,20,42,44);copyAB.setBounds(1082,20,92,44);slotB.setBounds(1184,20,42,44);
  bypass.setBounds(1230,20,220,44);
  presetBrowser.setBounds(0,0,1480,1060);
  enginePick.setBounds(135,88,280,42);
  experimental.setBounds(426,100,140,18);
  const int cr=showWhy?875:1450;const int cw=cr-31;const int col=cw/3;const int knobY=410,knob=200;
  speed.setBounds(31+col/2-knob/2,knobY,knob,knob);
  sustain.setBounds(31+col+col/2-knob/2,knobY,knob,knob);
  amount.setBounds(31+2*col+col/2-knob/2,knobY,knob,knob);
  const int slot=(cr-92)/4;
  doubler.setBounds(62,640,slot-24,26);width.setBounds(62+slot,640,slot-24,26);
  stabilize.setBounds(62+2*slot+6,636,150,28);reference.setBounds(62+3*slot+10,640,slot-30,26);
  key.setBounds(62,720,95,40);major.setBounds(170,720,58,40);minor.setBounds(234,720,58,40);chromatic.setBounds(298,720,82,40);scalePick.setBounds(388,720,122,40);
  shift.setBounds((int)(cr*.66f-60),720,120,40);range.setBounds((int)(cr*.80f-60),720,120,40);
  why.setBounds(cr-220,720,190,40);why.setVisible(!showWhy);
  historyPanel.setBounds(889,135,561,890);
  lowLatency.setBounds(-200,-200,10,10);
  const int px=31,py=790,pw=cr-31,ph=210;const float kw=(pw-24)/7.f;
  const int white[]={0,2,4,5,7,9,11};
  for(int j=0;j<7;++j)notes[white[j]].setBounds((int)(px+12+j*kw),py+10,(int)kw-2,ph-20);
  const int black[]={1,3,6,8,10};const int gaps[]={1,2,4,5,6};
  for(int j=0;j<5;++j){const int bx=(int)(px+12+gaps[j]*kw-kw*.29f);notes[black[j]].setBounds(bx,py+10,(int)(kw*.58f),ph*.62f);notes[black[j]].toFront(false);}
 }
 void paintCanvas(juce::Graphics& g){
  const auto ink=juce::Colour(0xfff4f1ff),muted=juce::Colour(0xff8d879d),accent=juce::Colour(0xffb592ff),accentDeep=juce::Colour(0xff7c5cff),line=juce::Colour(0xff2d2939);
  juce::ColourGradient background(juce::Colour(0xff101116),0,0,juce::Colour(0xff17121f),1480,1060,false);g.setGradientFill(background);g.fillAll();
  juce::ColourGradient bar(juce::Colour(0xff171821),0,0,juce::Colour(0xff101116),0,84,false);g.setGradientFill(bar);g.fillRect(0,0,1480,84);
  g.setColour(line);g.fillRect(0,83,1480,1);
  for(int x:{798,1018,1224})g.fillRect(x,26,1,32);
  juce::ColourGradient mark(accentDeep,28,22,accent,62,56,false);g.setGradientFill(mark);g.fillRoundedRectangle(28,22,34,34,10);
  g.setColour(juce::Colour(0xff17121f));g.setFont(juce::Font(18.f,juce::Font::bold));g.drawText("M",28,22,34,34,juce::Justification::centred);
  g.setColour(ink);g.setFont(juce::Font(26.f,juce::Font::bold));g.drawText("MegaTune",74,18,180,40,juce::Justification::left);
  juce::ColourGradient engineBg(juce::Colour(0xff121319),0,84,juce::Colour(0xff121319),0,135,false);g.setGradientFill(engineBg);g.fillRect(0,84,1480,51);
  g.setColour(muted);g.setFont(juce::Font(13.f));g.drawText("ENGINE",62,99,64,20,juce::Justification::left);
  const int lat=proc.getLatencySamples();const double srate=proc.getSampleRate();
  g.setFont(juce::Font(15.f));g.setColour(muted);g.drawText((lat>0&&srate>0)?(juce::String(lat*1000.0/srate,1)+" ms"):juce::String("-- ms"),1280,98,160,20,juce::Justification::right);
  const int cr=showWhy?875:1450,cw=cr-31;
  auto card=[&](float x,float y,float w,float h,float radius){juce::ColourGradient c(juce::Colour(0xff1a1824),x,y,juce::Colour(0xff111018),x,y+h,false);g.setGradientFill(c);g.fillRoundedRectangle(x,y,w,h,radius);g.setColour(juce::Colour(0xff332d42));g.drawRoundedRectangle(x,y,w,h,radius,1.f);};
  card(31,135,(float)cw,220.f,14.f);
  const float p=proc.pitch.load();const int targetNote=megatune::currentTargetNote(proc);const bool bypassed=proc.value("bypass")>.5f;
  // Readout: the note being corrected to, and how far the input is from it.
  g.setColour(muted);g.setFont(juce::Font(12.f));g.drawText("CORRECTING TO",51,150,260,16,juce::Justification::centred);
  g.setColour(targetNote>0&&!bypassed?ink:muted);g.setFont(juce::Font(78.f,juce::Font::bold));
  g.drawText(targetNote>0?megaTuneNoteName((float)targetNote):juce::String("--"),51,166,260,86,juce::Justification::centred);
  if(p>0&&targetNote>0){const float cents=megaTuneCentsBetween(p,(float)targetNote);
   g.setColour(muted);g.setFont(juce::Font(16.f));g.drawText("input "+megaTuneNoteName(p),51,256,126,22,juce::Justification::right);
   g.setColour(accent);g.setFont(juce::Font(16.f,juce::Font::bold));g.drawText((cents>=0?"+":"")+juce::String(cents,1)+" cents",187,256,124,22,juce::Justification::left);}
  else{g.setColour(muted);g.setFont(juce::Font(16.f));g.drawText("waiting for pitch",51,256,260,22,juce::Justification::centred);}
  const float mx=61,mw=240,my=300;
  g.setColour(juce::Colour(0xff2a2637));g.fillRoundedRectangle(mx,my-4,mw,8,4);
  g.setColour(line);g.fillRect(juce::Rectangle<float>(mx+mw*.5f-1,my-10,2,20));
  for(int k=-2;k<=2;++k){const float x=mx+mw*(k+2)/4.f;g.setColour(muted.withAlpha(.55f));g.fillRect(juce::Rectangle<float>(x,my-3,1.f,6.f));}
  if(p>0&&targetNote>0){const float x=mx+mw*megaTuneMeterFraction(megaTuneCentsBetween(p,(float)targetNote));g.setColour(accent.withAlpha(.22f));g.fillRoundedRectangle(x-8,my-12,16,24,8);g.setColour(accent);g.fillRoundedRectangle(x-2,my-9,4,18,2);}
  g.setColour(muted);g.setFont(juce::Font(10.f));g.drawText("flat",mx-10,my+13,40,14,juce::Justification::left);g.drawText("in tune",mx+mw/2-30,my+13,60,14,juce::Justification::centred);g.drawText("sharp",mx+mw-30,my+13,40,14,juce::Justification::right);
  g.setColour(line);g.fillRect(331,155,1,180);
  // Pitch trace: the last few seconds of input pitch and corrected pitch over the scale's note lanes.
  const float gx=351,gy=151,gw=(float)(cr-351-70),gh=188;const juce::Rectangle<float> graph(gx,gy,gw,gh);
  const auto inputColour=juce::Colour(0xff8ee7ff);
  g.setColour(juce::Colour(0xff0d0c13));g.fillRoundedRectangle(graph,10);
  {juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(graph.toNearestInt());
   const float lane=gh/14.f;const int root=(int)proc.value("key");
   auto yOf=[&](float midi){return gy+gh*.5f-(midi-graphCentre)*lane;};
   for(int n=(int)std::floor(graphCentre-8);n<=(int)std::ceil(graphCentre+8);++n){
    const bool enabled=proc.noteValue(((n-root)%12+12)%12)>.5f;const bool isTarget=n==targetNote&&!bypassed;const float y=yOf((float)n);
    if(enabled){g.setColour(isTarget?accent.withAlpha(.22f):juce::Colours::white.withAlpha(.045f));g.fillRect(juce::Rectangle<float>(gx,y-lane*.5f+1,gw,lane-2));
     g.setColour(isTarget?ink:muted);g.setFont(juce::Font(10.f));g.drawText(megaTuneNoteName((float)n),juce::Rectangle<float>(gx+8,y-7,34,14),juce::Justification::left);}
    else{g.setColour(juce::Colours::white.withAlpha(.03f));g.fillRect(juce::Rectangle<float>(gx,y,gw,1));}}
   auto trace=[&](const std::array<float,graphN>& data,juce::Colour colour,float thickness){juce::Path path;bool open=false;float last=0;
    for(int i=0;i<graphN;++i){const float v=data[(size_t)((graphHead+i)%graphN)];
     if(v<=0){open=false;continue;}if(open&&std::abs(v-last)>7.f)open=false;
     const float x=gx+46+(gw-54)*(float)i/(float)(graphN-1),y=yOf(v);if(open)path.lineTo(x,y);else path.startNewSubPath(x,y);open=true;last=v;}
    g.setColour(colour);g.strokePath(path,juce::PathStrokeType(thickness,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));};
   trace(inTrace,inputColour.withAlpha(.85f),1.5f);if(!bypassed)trace(outTrace,accent,2.6f);}
  g.setColour(juce::Colour(0xff332d42));g.drawRoundedRectangle(graph,10,1);
  g.setFont(juce::Font(11.f));g.setColour(inputColour);g.fillEllipse(gx+gw-166,gy+12,7,7);g.setColour(muted);g.drawText("Input",(int)(gx+gw-154),(int)gy+8,40,15,juce::Justification::left);
  g.setColour(accent);g.fillEllipse(gx+gw-106,gy+12,7,7);g.setColour(muted);g.drawText("Corrected",(int)(gx+gw-94),(int)gy+8,70,15,juce::Justification::left);
  if(bypassed){g.setColour(accent);g.setFont(juce::Font(15.f,juce::Font::bold));g.drawText("BYPASSED",graph.withTrimmedLeft(46),juce::Justification::centred);}
  else if(!graphEver){g.setColour(muted);g.setFont(juce::Font(14.f));g.drawText("Play audio to see the pitch trace",graph.withTrimmedLeft(46),juce::Justification::centred);}
  auto meter=[&](float x,float v,const char* label){const juce::Rectangle<float> track(x,gy,10,gh-18);g.setColour(juce::Colour(0xff0d0c13));g.fillRoundedRectangle(track,4);
   const float h=track.getHeight()*v;if(h>1){g.setGradientFill(juce::ColourGradient(accentDeep,x,track.getBottom(),inputColour,x,track.getY(),false));g.fillRoundedRectangle(x,track.getBottom()-h,10,h,4);}
   g.setColour(muted);g.setFont(juce::Font(10.f));g.drawText(label,juce::Rectangle<float>(x-10,gy+gh-14,30,14),juce::Justification::centred);};
  meter((float)cr-54,inMeter,"IN");meter((float)cr-32,outMeter,"OUT");
  card(31,370,(float)cw,300.f,14.f);
  const int col=cw/3;const int slot=(cr-92)/4;
  g.setColour(line);for(int i=1;i<3;++i)g.fillRect(31+col*i,390,1,216);
  g.setColour(muted);g.setFont(juce::Font(13.f));
  const char* names[]={"SPEED","SUSTAIN","AMOUNT"};for(int i=0;i<3;++i)g.drawText(names[i],31+col*i,378,col,18,juce::Justification::centred);
  {const char* hints[]={"slower  /  tighter","relax held notes  /  tighten","how much correction"};g.setFont(juce::Font(11.f));g.setColour(muted.withAlpha(.7f));for(int i=0;i<3;++i)g.drawText(hints[i],31+col*i,395,col,14,juce::Justification::centred);g.setColour(muted);}
  g.setFont(juce::Font(12.f));
  g.drawText("DOUBLER",62,620,110,16,juce::Justification::left);g.drawText("STEREO WIDTH",62+slot,620,140,16,juce::Justification::left);
  g.drawText("NOTE STABILIZER",62+2*slot+6,620,170,16,juce::Justification::left);g.drawText("PITCH REFERENCE",62+3*slot+10,620,150,16,juce::Justification::left);
  g.setColour(muted);g.setFont(juce::Font(12.f));
  g.drawText("KEY",62,700,95,16,juce::Justification::left);g.drawText("SCALE",170,700,210,16,juce::Justification::left);
  const int shiftX=(int)(cr*.66f-60),rangeX=(int)(cr*.80f-60);
  g.drawText("SHIFT",shiftX,700,120,16,juce::Justification::left);g.drawText("RANGE",rangeX,700,120,16,juce::Justification::left);
  card(31,790,(float)cw,210.f,14.f);
  // footer
  g.setColour(line);g.fillRect(0,1028,1480,1);
  g.setColour(muted);g.setFont(juce::Font(11.f));g.drawText("ENGINE",40,1038,56,18,juce::Justification::left);
  g.setColour(ink);g.setFont(juce::Font(16.f,juce::Font::bold));g.drawText(proc.value("smith")>.5f&&proc.value("lowlatency")<=.5f?"Signalsmith":proc.value("lowlatency")>.5f?"Low Latency":proc.value("engine")>.5f?"Quality":"Quality (classic)",104,1036,170,22,juce::Justification::left);
  g.setColour(line);g.fillRect(280,1038,1,18);
  g.setColour(muted);g.setFont(juce::Font(14.f));g.drawText("ademusic MegaTune v"+juce::String(JucePlugin_VersionString),300,1036,260,22,juce::Justification::left);
  g.setColour(line);g.fillRect(520,1038,1,18);
  const int root=(int)proc.value("key");const char* rootNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  const int legY=1046;
  g.setColour(accent);g.fillRoundedRectangle(650,1038,18,18,5);g.setColour(juce::Colour(0xff17121f));g.setFont(juce::Font(12.f,juce::Font::bold));g.drawText("✓",650,1038,18,18,juce::Justification::centred);
  g.setColour(ink);g.setFont(juce::Font(13.f));g.drawText("Enabled",676,1038,80,18,juce::Justification::left);
  g.setColour(juce::Colour(0xff8ee7ff));g.drawRoundedRectangle(782,1040,16,14,4,2.f);g.setColour(ink);g.drawText("Input",806,1038,60,18,juce::Justification::left);
  g.setColour(accent);g.fillRoundedRectangle(884,1039,18,16,5);g.setColour(ink);g.drawText("Target",910,1038,70,18,juce::Justification::left);
  int scaleMask=0;for(int n=0;n<12;++n)if(proc.noteValue(n)>.5f)scaleMask|=1<<n;
  const juce::String scaleName=megaTuneScaleName(scaleMask);
  g.setColour(accent);g.fillEllipse(995,1042,9,9);g.setColour(ink);g.drawText("Scale note ("+juce::String(rootNames[root])+" "+scaleName+")",1012,1038,240,18,juce::Justification::left);
  g.setColour(line);g.fillRect(1250,1038,1,18);
  g.setColour(accent);for(int i=0;i<4;++i){const float h=5.f+i*4.f;g.fillRoundedRectangle(1272+i*5,1049-h,3,h,1);}
  g.setColour(ink);g.setFont(juce::Font(14.f));g.drawText(p>0?"Audio: Active":"Audio: Idle",1294,1036,140,22,juce::Justification::left);
 } };
namespace megatune {
int historySequence(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->historySeq.load();return 0;}
DecisionView historyDecision(const juce::AudioProcessor& processor,int sequence){DecisionView v{};if(auto* tune=dynamic_cast<const MegaTune*>(&processor))tune->readDecision(sequence,v);return v;}
void appendDecision(juce::AudioProcessor& processor,float in,float target,int reason){if(auto* tune=dynamic_cast<MegaTune*>(&processor))tune->recordDecision(in,target,reason);}
int currentTargetNote(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->currentTargetNote.load();return 0;}
int smithBufferCapacity(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->smithBufferCapacity();return 0;}
void setKeepKey(juce::AudioProcessor& processor,bool keep){if(auto* tune=dynamic_cast<MegaTune*>(&processor))tune->keepKey=keep;}
bool saveUserPreset(juce::AudioProcessor& processor,const juce::String& name){if(auto* tune=dynamic_cast<MegaTune*>(&processor))return tune->saveUserPreset(name);return false;}
bool loadUserPreset(juce::AudioProcessor& processor,const juce::String& name){if(auto* tune=dynamic_cast<MegaTune*>(&processor))return tune->loadUserPreset(name);return false;}
bool deleteUserPreset(juce::AudioProcessor& processor,const juce::String& name){if(auto* tune=dynamic_cast<MegaTune*>(&processor))return tune->deleteUserPreset(name);return false;}
void tickEditor(juce::AudioProcessorEditor& editor){if(auto* view=dynamic_cast<Editor*>(&editor))view->tick();}
}
juce::AudioProcessorEditor* MegaTune::createEditor(){return new Editor(*this);}juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MegaTune();}
