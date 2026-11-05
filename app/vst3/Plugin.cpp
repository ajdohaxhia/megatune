#include <juce_audio_utils/juce_audio_utils.h>
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
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"engine",1},"TD-PSOLA",false));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"smith",1},"Signalsmith",false));
  p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"smithcomp",1},"Smith Comp",true));
  return p;
 }
 MegaTune():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"MegaTune",parameters()){const char* ids[]={"speed","sustain","amount","bypass","stabilize","key","doubler","width","reference","lowlatency","shift","range","engine","smith","smithcomp"};for(int i=0;i<15;++i)raw[i]=state.getRawParameterValue(ids[i]);for(int i=0;i<12;++i)noteRaw[i]=state.getRawParameterValue("note"+juce::String(i));}
 const juce::String getName()const override{return "MegaTune";}
 bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;} double getTailLengthSeconds()const override{return 0;}
 std::atomic<int> currentProgram{0};
 struct FactoryPreset{const char* name;float speed,sustain,amount,doubler,width;bool stabilize;int key;int mask;};
 static const FactoryPreset* factoryPresets(){static const FactoryPreset p[]={
  {"Default",25,0,100,0,0,true,4,1453},
  {"Natural Tune",90,-20,70,0,0,true,0,2741},
  {"Modern Pop",45,-10,95,8,12,true,0,2741},
  {"Polished Pop",70,-25,85,10,20,true,0,2741},
  {"R&B Smooth",120,-40,75,15,25,true,9,1453},
  {"Hip-Hop Lead",25,10,100,5,10,true,0,1453},
  {"Doubler Wide",25,0,100,65,100,true,0,2741},
  {"Trap Robotic",-3,40,100,0,0,false,0,4095},
  {"EDM Vocal",15,20,100,20,45,true,0,2741},
  {"Rock Anthem",60,-15,90,12,30,true,4,1453},
  {"Indie Folk",150,-45,60,0,0,true,0,2741},
  {"Lo-Fi Bed",200,-60,55,35,60,true,0,2741},
  {"Doubler Tight",30,0,100,35,15,true,0,2741},
  {"Subtle Assist",180,-30,40,0,0,true,0,2741},
  {"Extreme Tune",-3,60,100,0,0,false,0,4095},
  {"Spoken Word",100,-10,50,0,0,true,0,4095}
 };return p;}
 int getNumPrograms()override{return 16;} int getCurrentProgram()override{return currentProgram.load();}
 const juce::String getProgramName(int i)override{return factoryPresets()[juce::jlimit(0,15,i)].name;}
 void setCurrentProgram(int i)override{if(i<0||i>=getNumPrograms())return;currentProgram=i;const auto&p=factoryPresets()[i];
  auto set=[&](const char* id,float v){auto* param=state.getParameter(id);param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1(v));param->endChangeGesture();};
  set("speed",p.speed);set("sustain",p.sustain);set("amount",p.amount);set("doubler",p.doubler);set("width",p.width);
  {auto* param=state.getParameter("stabilize");param->beginChangeGesture();param->setValueNotifyingHost(p.stabilize?1.f:0.f);param->endChangeGesture();}
  {auto* param=state.getParameter("key");param->beginChangeGesture();param->setValueNotifyingHost((float)p.key/11.f);param->endChangeGesture();}
  for(int n=0;n<12;++n){auto* param=state.getParameter("note"+juce::String(n));param->beginChangeGesture();param->setValueNotifyingHost((p.mask&(1<<n))?1.f:0.f);param->endChangeGesture();}}
void changeProgramName(int,const juce::String&)override{}
 bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainInputChannelSet()==l.getMainOutputChannelSet()&&(l.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
 void prepareToPlay(double r,int samplesPerBlock)override {rate=r;write=0;analysisCount=0;phase=0;ratio=1;target=0;lastNote=0;heldSeconds=0;doublePhase=0;currentTargetNote=0;psolaGain=0;psolaVoiced=false;voicedHold=0;pitchCount=0;for(auto& c:doubleRing)c.fill(0); for(auto& c:ring)c.fill(0); history.fill(0);hist=0;lowLatencyActive=value("lowlatency")>.5f;window=lowLatencyActive?128:2048;setLatencySamples(window);yinWorkspace.ensure(512);psola[0].prepare(window);psola[1].prepare(window);if(!smithReady||smithRate!=r){smith.presetDefault(2,(float)rate);smithReady=true;smithRate=r;}smith.reset();smithLatency=smith.inputLatency()+smith.outputLatency();if(value("smith")>.5f&&!lowLatencyActive)setLatencySamples(smithLatency);const int staging=juce::jmax(8192,samplesPerBlock);sIn[0].assign((size_t)staging,0.f);sIn[1].assign((size_t)staging,0.f);sOut[0].assign((size_t)staging,0.f);sOut[1].assign((size_t)staging,0.f);}
 void releaseResources()override{}
 void reset()override{write=0;hist=0;analysisCount=0;phase=0;ratio=1;target=0;lastNote=0;heldSeconds=0;doublePhase=0;currentTargetNote=0;pitch=0;outputPitch=0;level=0;outputLevel=0;pitchCount=0;pitchHist[0]=pitchHist[1]=pitchHist[2]=0;psolaPeriod=200;psolaGain=0;psolaVoiced=false;voicedHold=0;for(auto& c:ring)c.fill(0);for(auto& c:doubleRing)c.fill(0);history.fill(0);psola[0].reset();psola[1].reset();if(smithReady)smith.reset();}
 float value(const char* id) const{const char* ids[]={"speed","sustain","amount","bypass","stabilize","key","doubler","width","reference","lowlatency","shift","range","engine","smith","smithcomp"};for(int i=0;i<15;++i)if(std::strcmp(id,ids[i])==0)return raw[i]->load();auto* v=state.getRawParameterValue(id);return v?v->load():0;}
 float noteValue(int i)const{return noteRaw[i]->load();}
 int smithBufferCapacity()const{return (int)std::min(sIn[0].capacity(),sOut[0].capacity());}
 juce::AudioProcessorParameter* getBypassParameter()const override{return state.getParameter("bypass");}
 enum DecisionReason{R_NEAREST=0,R_HELD=1,R_RANGE=2,R_UNVOICED=3,R_NONOTE=4};
 struct Decision{float in;float target;int reason;};
 std::array<Decision,16> decisions{};std::atomic<int> historySeq{0};std::atomic<int> currentTargetNote{0};
 void recordDecision(float in,float target,int reason){megatune::lastTargetPc.store(target>0?(((int)std::lround(target))%12+12)%12:-1,std::memory_order_relaxed);const int i=historySeq.load(std::memory_order_relaxed)&15;decisions[(size_t)i]={in,target,reason};historySeq.fetch_add(1,std::memory_order_relaxed);}
 std::atomic<float> pitch{0},outputPitch{0},level{0},outputLevel{0};
 void analyse(){
  constexpr int n=768; std::array<float,n> x{};float energy=0;
  const int stride=juce::jmax(1,(int)(rate/12000));
  for(int i=0;i<n;++i){x[i]=history[(hist-1-i*stride+16384)%16384];energy+=x[i]*x[i];}
  if(energy/n<0.00001f){pitch=0;target=0;lastNote=0;heldSeconds=0;pitchCount=0;currentTargetNote=0;recordDecision(0,0,R_UNVOICED);return;}
  const float sr=(float)rate/stride;
  const auto yin=megatune::detectYin(x.data(),n,sr,70.f,1000.f,.15f,yinWorkspace);
  if(!yin.voiced){pitch=0;target=0;lastNote=0;heldSeconds=0;pitchCount=0;currentTargetNote=0;recordDecision(0,0,R_UNVOICED);return;}
  float midi=69+12*std::log2(yin.hz/juce::jmax(1.f,value("reference")));pitch=midi;
  if(pitchCount<3)pitchHist[pitchCount++]=midi;else{pitchHist[0]=pitchHist[1];pitchHist[1]=pitchHist[2];pitchHist[2]=midi;const float lo=std::min(pitchHist[0],std::min(pitchHist[1],pitchHist[2])),hi=std::max(pitchHist[0],std::max(pitchHist[1],pitchHist[2]));midi=pitchHist[0]+pitchHist[1]+pitchHist[2]-lo-hi;}
  float distance=100;int nearest=0;for(int i=(int)std::floor(midi)-12;i<=(int)std::ceil(midi)+12;++i)if(noteValue((i-(int)value("key")+24)%12)>.5f&&std::abs(i-midi)<distance){distance=std::abs(i-midi);nearest=i;}
  if(distance==100){target=0;lastNote=0;heldSeconds=0;currentTargetNote=0;recordDecision(midi,0,R_NONOTE);return;}if(value("stabilize")>.5f&&lastNote>0&&std::abs(midi-lastNote)<.6f&&noteValue((lastNote-(int)value("key")+24)%12)>.5f)nearest=lastNote;{unsigned m=0;for(int q=0;q<12;++q)if(noteRaw[q]->load()>.5f)m|=1u<<q;megatune::debugMask.store(m);megatune::debugNearest.store(nearest);megatune::debugMidi.store((int)std::lround(midi*100.f));megatune::debugKey.store((int)value("key"));}const int previousNote=lastNote;if(nearest!=lastNote)heldSeconds=0;const bool stabilizeOn=value("stabilize")>.5f;lastNote=nearest;currentTargetNote=nearest;const bool held=(stabilizeOn&&nearest>0&&nearest==previousNote);const float correction=(float)(nearest-midi);const bool outOfRange=std::abs(correction)>value("range");
  target=(outOfRange?0.f:correction)*value("amount")*.01f+value("shift");
  recordDecision(midi,(float)nearest,outOfRange?R_RANGE:(held?R_HELD:R_NEAREST));psolaPeriod=(float)(rate/juce::jmax(1.f,yin.hz));const float psRatio=std::pow(2.f,target/12);psola[0].setTarget(psolaPeriod,psRatio,yin.voiced);psola[1].setTarget(psolaPeriod,psRatio,yin.voiced);if(yin.voiced) voicedHold=(int)(rate*0.12);
 }
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{processAudio(b,false);}
 void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{processAudio(b,true);}
 void processAudio(juce::AudioBuffer<float>& b,bool hostBypass){
  const bool smithActive=value("smith")>.5f&&!lowLatencyActive;
  if(!hostBypass&&value("bypass")<=.5f&&smithActive){const int n=b.getNumSamples();
   if(n>(int)sIn[0].size()){const size_t size=(size_t)n;for(auto& channel:sIn)channel.assign(size,0.f);for(auto& channel:sOut)channel.assign(size,0.f);}
   const int channels=juce::jmin(2,b.getNumChannels());if(channels<=0)return;const int second=juce::jmin(1,channels-1);float peak=0;
   for(int i=0;i<n;++i){float mono=0;for(int c=0;c<channels;++c){const float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0.f;mono+=ring[c][write]/channels;peak=juce::jmax(peak,std::abs(ring[c][write]));}
    history[hist]=mono;hist=(hist+1)%16384;if(++analysisCount>=512){analysisCount=0;analyse();}write=(write+1)%ringSize;
    const float in0=b.getSample(0,i),in1=b.getSample(second,i);sIn[0][i]=std::isfinite(in0)?in0:0.f;sIn[1][i]=std::isfinite(in1)?in1:0.f;}
   smith.setTransposeSemitones((float)target,0.f);
   smith.setFormantSemitones(0.f,value("smithcomp")>.5f);
   const float* ip[2]={sIn[0].data(),sIn[1].data()};float* op[2]={sOut[0].data(),sOut[1].data()};
   smith.process(ip,n,op,n);
   const int outputStart=(write-(n%ringSize)+ringSize)%ringSize;float outPeak=0;
   for(int i=0;i<n;++i){const int frame=(outputStart+i)%ringSize;
    for(int c=0;c<channels;++c){float out=sOut[(size_t)c][i];if(!std::isfinite(out))out=0.f;doubleRing[c][frame]=out;
     if(value("bypass")<=.5f){const float wet=value("doubler")*.005f;const float width=value("width")*.01f;const float delay=juce::jlimit(1.f,16000.f,(18.f+(c==0?-6.f:6.f)*width+std::sin(doublePhase+(c==0?0.f:1.7f))*1.2f)*(float)rate*.001f);float pos=frame-delay;while(pos<0)pos+=ringSize;const int index=(int)pos;const float fraction=pos-index;const float doubled=doubleRing[c][index]*(1-fraction)+doubleRing[c][(index+1)%ringSize]*fraction;out=out*(1-wet)+doubled*wet;}
     outPeak=juce::jmax(outPeak,std::abs(out));b.setSample(c,i,out);}
    doublePhase+=1.7f/(float)rate;if(doublePhase>juce::MathConstants<float>::twoPi)doublePhase-=juce::MathConstants<float>::twoPi;}
   level=peak;outputLevel=outPeak;outputPitch=pitch.load()>0?pitch.load()+target:0.f;
   if(getLatencySamples()!=smithLatency)setLatencySamples(smithLatency);
   return;}


  juce::ScopedNoDenormals no;const int channels=juce::jmin(2,b.getNumChannels());if(channels<=0)return;float peak=0,outPeak=0;bool bypass=hostBypass||value("bypass")>.5f;
  const int requestedWindow=value("lowlatency")>.5f?128:2048;
  if(smithActive){if(getLatencySamples()!=smithLatency)setLatencySamples(smithLatency);}
  else if(requestedWindow!=window){window=requestedWindow;lowLatencyActive=window==128;setLatencySamples(window);}
  const int half=window/2;const int dryDelay=smithActive?smithLatency:window;
  for(int i=0;i<b.getNumSamples();++i){float mono=0;for(int c=0;c<channels;++c){float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0;mono+=ring[c][write]/channels;peak=juce::jmax(peak,std::abs(ring[c][write]));}history[hist]=mono;hist=(hist+1)%16384;
   if(++analysisCount>=512){analysisCount=0;analyse();}
   if(lastNote>0)heldSeconds=juce::jmin(1.f,heldSeconds+1.f/(float)rate);
   const float blend=megaTuneRatioBlend(value("speed"),value("sustain"),heldSeconds,(float)rate);ratio+=(std::pow(2.f,target/12)-ratio)*blend;
   phase+=1-ratio;while(phase<0)phase+=window;while(phase>=window)phase-=window;
   for(int c=0;c<channels;++c){auto read=[&](float delay){float pos=write-delay;while(pos<0)pos+=ringSize;int a=(int)pos;float t=pos-a;
     auto tap=[&](int k){return ring[c][(a+k+ringSize)%ringSize];};float y0=tap(-1),y1=tap(0),y2=tap(1),y3=tap(2);
     return y1+.5f*t*(y2-y0+t*(2.f*y0-5.f*y1+4.f*y2-y3+t*(3.f*(y1-y2)+y3-y0)));};float out;
    const float ps=psola[c].process(ring[c][write]);
    if(voicedHold>0)--voicedHold;psolaVoiced=voicedHold>0;psolaGain+=((psolaVoiced?1.f:0.f)-psolaGain)*(psolaVoiced?0.002f:0.0002f);
    if(bypass||(value("amount")==0&&value("shift")==0))out=read(dryDelay);
    else if(!lowLatencyActive&&value("engine")>.5f)out=psolaGain*ps+(1.f-psolaGain)*read(window);
    else if(lowLatencyActive){float p2=std::fmod(phase+half,(float)window);float w=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*phase/window);out=read(half+phase)*w+read(half+p2)*(1-w);}
    else{const float base=(float)(half+window/4);float acc=0;for(int k=0;k<4;++k){float g=std::fmod(phase+k*window*.25f,(float)window);float wt=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*g/window);acc+=wt*read(base+g);}out=acc*.5f;}
    doubleRing[c][write]=out;
    if(!bypass){float wet=value("doubler")*.005f;float width=value("width")*.01f;float delay=(18.f+(c==0?-6.f:6.f)*width+std::sin(doublePhase+(c==0?0.f:1.7f))*1.2f)*(float)rate*.001f;delay=juce::jlimit(1.f,16000.f,delay);float pos=write-delay;while(pos<0)pos+=ringSize;int index=(int)pos;float fraction=pos-index;float doubled=doubleRing[c][index]*(1-fraction)+doubleRing[c][(index+1)%ringSize]*fraction;out=out*(1-wet)+doubled*wet;}
    outPeak=juce::jmax(outPeak,std::abs(out));b.setSample(c,i,out);}
   doublePhase+=1.7f/(float)rate;if(doublePhase>juce::MathConstants<float>::twoPi)doublePhase-=juce::MathConstants<float>::twoPi;write=(write+1)%ringSize;
  }level=peak;outputLevel=outPeak;float detected=pitch.load();outputPitch=detected>0?detected+((bypass||value("amount")==0)?0.f:12.f*std::log2(juce::jmax(.001f,ratio))):0.f;
 }
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override;
 void getStateInformation(juce::MemoryBlock& m)override{auto saved=state.copyState();saved.setProperty("factoryProgram",currentProgram.load(),nullptr);auto xml=saved.createXml();copyXmlToBinary(*xml,m);}
 void setStateInformation(const void* d,int n)override{auto xml=getXmlFromBinary(d,n);if(xml&&xml->hasTagName(state.state.getType())){auto saved=juce::ValueTree::fromXml(*xml);currentProgram=juce::jlimit(0,getNumPrograms()-1,(int)saved.getProperty("factoryProgram",0));state.replaceState(saved);}}
private:
 std::array<std::atomic<float>*,15> raw{};std::array<std::atomic<float>*,12> noteRaw{};
 double rate=44100;int write=0,hist=0,analysisCount=0,lastNote=0,window=2048,pitchCount=0;float pitchHist[3]{};megatune::Psola psola[2];float psolaPeriod=200.f,psolaGain=0.f;bool psolaVoiced=false;int voicedHold=0;signalsmith::stretch::SignalsmithStretch<float> smith;bool smithReady=false;double smithRate=0;std::vector<float> sIn[2],sOut[2];int smithLatency=0;megatune::YinWorkspace yinWorkspace;bool lowLatencyActive=false;float phase=0,ratio=1,target=0,heldSeconds=0;
 static constexpr int ringSize=131072;std::array<std::array<float,ringSize>,2> ring{};std::array<float,16384> history{};std::array<std::array<float,ringSize>,2> doubleRing{};float doublePhase=0;
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
   if(s>=0&&s<seq){const auto& d=proc.decisions[(size_t)(s&15)];row.input=d.in;row.target=d.target;row.reason=d.reason;row.selected=s==selectedSeq;row.setVisible(true);}
   else{row.selected=false;row.setVisible(false);}
   row.repaint();}
  if(selectedSeq>=0&&selectedSeq<seq){const auto& d=proc.decisions[(size_t)(selectedSeq&15)];if(d.target>0){actionPc=(((int)std::lround(d.target))%12+12)%12;const int root=(int)proc.value("key");const int degree=((actionPc-root)%12+12)%12;actionEnabled=proc.noteValue(degree)>.5f;}}
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

class PresetRow final:public juce::Button {
public:
 explicit PresetRow(int i):Button("Preset row"){index=i;setWantsKeyboardFocus(true);}
 int index=0;bool selected=false;
 void paintButton(juce::Graphics& g,bool over,bool)override{
  auto r=getLocalBounds().toFloat();
  if(selected||over){g.setColour(selected?juce::Colour(0xff8b5cf6):juce::Colour(0xff221f2d));g.fillRoundedRectangle(r.reduced(0,1),7);}
  g.setColour(selected?juce::Colour(0xff17121f):juce::Colour(0xfff4f1ff));g.setFont(juce::Font(16.f,selected?juce::Font::bold:juce::Font::plain));
  g.drawText(getButtonText(),r.withTrimmedLeft(14),juce::Justification::centredLeft);
  if(!selected){g.setColour(juce::Colour(0xff2d2939));g.fillRect(juce::Rectangle<float>(r.getX()+8,r.getBottom()-1,r.getWidth()-16,1));}
 }
};
class PresetBrowser final:public juce::Component {
public:
 PresetBrowser(MegaTune& p):proc(p){
  setComponentID("preset-browser");setOpaque(false);setInterceptsMouseClicks(true,true);setWantsKeyboardFocus(true);
  addAndMakeVisible(search);search.setTextToShowWhenEmpty("Search presets",juce::Colour(0xff8d879d));
  search.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff17151f));search.setColour(juce::TextEditor::textColourId,juce::Colour(0xfff4f1ff));
  search.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff383047));search.setColour(juce::TextEditor::focusedOutlineColourId,juce::Colour(0xffa78bfa));
  search.setFont(juce::Font(16.f));search.onTextChange=[this]{filter();};
  addAndMakeVisible(list);list.setScrollBarThickness(8);list.setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff5a4b7a));list.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff5a4b7a));
  addAndMakeVisible(close);close.setButtonText("X");close.onClick=[this]{hide();};
  addAndMakeVisible(cancel);cancel.setButtonText("Cancel");cancel.onClick=[this]{hide();};
  addAndMakeVisible(load);load.setButtonText("Load preset");load.setComponentID("preset-load");load.onClick=[this]{if(selectedIndex>=0&&onLoad)onLoad(selectedIndex);hide();};
  const auto* presets=MegaTune::factoryPresets();
  for(int i=0;i<proc.getNumPrograms();++i){auto* row=new PresetRow(i);row->setButtonText(presets[i].name);row->onClick=[this,i]{selectedIndex=i;refreshRows();repaint();};listContent.addAndMakeVisible(row);rows.add(row);}
  list.setViewedComponent(&listContent,false);
  selectedIndex=proc.getCurrentProgram();refreshRows();
 }
 void show(){selectedIndex=proc.getCurrentProgram();search.clear();refreshRows();setVisible(true);toFront(true);grabKeyboardFocus();}
 void hide(){setVisible(false);if(onClose)onClose();}
 void filter(){refreshRows();}
 void refreshRows(){
  const auto text=search.getText().trim().toLowerCase();int y=0;
  for(int i=0;i<rows.size();++i){auto* row=rows[i];const bool match=text.isEmpty()||row->getButtonText().toLowerCase().contains(text);row->setVisible(match);if(match){row->setBounds(0,y,list.getWidth()-10,42);row->selected=i==selectedIndex;row->repaint();y+=44;}}
  listContent.setSize(juce::jmax(1,list.getWidth()-10),juce::jmax(1,y));list.setViewedComponent(&listContent,false);
 }
 void resized()override{
  const int pw=960,ph=620;const int px=(getWidth()-pw)/2,py=(getHeight()-ph)/2;
  close.setBounds(px+pw-58,py+18,40,40);
  search.setBounds(px+32,py+104,440,44);
  list.setBounds(px+32,py+164,440,356);
  cancel.setBounds(px+604,py+ph-66,140,44);load.setBounds(px+762,py+ph-66,166,44);
  listContent.setSize(430,juce::jmax(1,rows.size()*44));refreshRows();
 }
 void paint(juce::Graphics& g)override{
  g.fillAll(juce::Colours::black.withAlpha(.58f));
  const int pw=960,ph=620;const int px=(getWidth()-pw)/2,py=(getHeight()-ph)/2;
  juce::Rectangle<float> panel((float)px,(float)py,(float)pw,(float)ph);
  juce::ColourGradient bg(juce::Colour(0xff1b1825),panel.getTopLeft(),juce::Colour(0xff0f0d15),panel.getBottomLeft(),false);g.setGradientFill(bg);g.fillRoundedRectangle(panel,16);
  g.setColour(juce::Colour(0xff3a324c));g.drawRoundedRectangle(panel,16,1);
  g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(28.f,juce::Font::bold));g.drawText("Factory presets",px+32,py+24,300,38,juce::Justification::left);
  g.setColour(juce::Colour(0xff2d2939));g.fillRect(px+250,py+30,1,28);
  g.setColour(juce::Colour(0xff9a94a8));g.setFont(juce::Font(15.f));g.drawText(juce::String(proc.getNumPrograms())+" presets",px+270,py+30,160,26,juce::Justification::left);
  g.setColour(juce::Colour(0xff2d2939));g.fillRect(px+32,py+82,pw-64,1);g.fillRect(px+496,py+100,1,ph-190);
  if(selectedIndex>=0&&selectedIndex<proc.getNumPrograms()){
   const auto& p=MegaTune::factoryPresets()[selectedIndex];
   g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(27.f,juce::Font::bold));g.drawText(p.name,px+528,py+104,380,38,juce::Justification::left);
   g.setColour(juce::Colour(0xff9a94a8));g.setFont(juce::Font(15.f));g.drawText("Factory preset",px+528,py+144,300,22,juce::Justification::left);
   static const char* keys[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
   const juce::String scale=p.mask==4095?"Chromatic":(p.mask==2741?"Major":(p.mask==1453?"Minor":"Custom"));
   const std::pair<juce::String,juce::String> fields[]={
    {"Speed",juce::String(p.speed,1)+" ms"},{"Sustain",juce::String(p.sustain,1)+"%"},{"Amount",juce::String(p.amount,1)+"%"},
    {"Doubler",juce::String(p.doubler,1)+"%"},{"Stereo Width",juce::String(p.width,1)+"%"},{"Note Stabilizer",p.stabilize?"On":"Off"},
    {"Key",keys[juce::jlimit(0,11,p.key)]},{"Scale",scale}};
   int y=py+180;for(auto& f:fields){g.setColour(juce::Colour(0xffd7d0e4));g.setFont(juce::Font(15.f));g.drawText(f.first,px+528,y,180,22,juce::Justification::left);
    g.setColour(juce::Colour(0xfff4f1ff));g.setFont(juce::Font(15.f,juce::Font::bold));g.drawText(f.second,px+700,y,200,22,juce::Justification::right);
    g.setColour(juce::Colour(0xff2d2939));g.fillRect(px+528,y+28,372,1);y+=34;}
   g.setColour(juce::Colour(0xff9a94a8));g.setFont(juce::Font(13.f));g.drawText("Loading a preset also recalls its key and enabled notes.",px+528,py+ph-122,390,20,juce::Justification::left);
  }
 }
 int selectedIndex=-1;std::function<void(int)> onLoad;std::function<void()> onClose;
 ~PresetBrowser()override{list.setViewedComponent(nullptr,false);}
private:
 MegaTune& proc;juce::TextButton close{"X"},cancel{"Cancel"},load{"Load preset"};juce::TextEditor search;
 juce::Component listContent;juce::Viewport list;juce::OwnedArray<PresetRow> rows;
};
class Editor final:public juce::AudioProcessorEditor,private juce::Timer {
 TunerLook tunerLook;MegaTune& proc;juce::Slider speed,sustain,amount,doubler,width,reference,shift,range;juce::ComboBox presets;juce::ToggleButton bypass{"Global Bypass"},stabilize{"Note Stabilizer"},lowLatency{"Low Latency"};juce::ComboBox key;int displayedKey=-1;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment;juce::TextButton major{"Major"},minor{"Minor"},chromatic{"Chromatic"};
 struct Sound {std::vector<float> values;int program=0;};
 juce::TextButton previous{"<"},next{">"},undo{"Undo"},redo{"Redo"},slotA{"A"},slotB{"B"},copyAB{"A > B"};juce::TextButton why{"Why this note?"},browse{"Browse"};bool showWhy=false;int selSeq=-1,selectedPc=-1,shownProgram=-1;HistoryPanel historyPanel;PresetBrowser presetBrowser;juce::Label experimental;juce::ComboBox enginePick;int shownEngine=-1;
 Sound comparison[2],observed;std::vector<Sound> past,future;int activeSlot=0;
 float animationTime=0,smoothedLevel=0;
 Sound capture()const{Sound sound;sound.program=proc.getCurrentProgram();for(auto* p:proc.getParameters())sound.values.push_back(p->getValue());return sound;}
 bool same(const Sound& a,const Sound& b)const{return a.program==b.program&&a.values==b.values;}
 void restore(const Sound& sound){proc.currentProgram=sound.program;auto& parameters=proc.getParameters();for(size_t i=0;i<sound.values.size()&&i<(size_t)parameters.size();++i){parameters[(int)i]->beginChangeGesture();parameters[(int)i]->setValueNotifyingHost(sound.values[i]);parameters[(int)i]->endChangeGesture();}observed=capture();repaint();}
 void remember(){auto now=capture();if(!same(now,observed)){past.push_back(observed);if(past.size()>100)past.erase(past.begin());future.clear();observed=now;}}
 void edit(const std::function<void()>& action){remember();auto before=capture();action();auto after=capture();if(!same(before,after)){past.push_back(before);future.clear();}observed=after;}
 void compare(int slot){comparison[activeSlot]=capture();activeSlot=slot;restore(comparison[slot]);past.clear();future.clear();}
 using SA=juce::AudioProcessorValueTreeState::SliderAttachment;using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
 std::unique_ptr<SA> a,b,c,da,wa,ra,sa,rga;std::unique_ptr<BA> d,e,ll;std::array<KeyButton,12> notes;std::array<std::unique_ptr<BA>,12> na;
 void syncKey(){const int root=(int)proc.value("key");if(root!=displayedKey){displayedKey=root;for(int i=0;i<12;++i){na[i].reset();na[i]=std::make_unique<BA>(proc.state,"note"+juce::String((i-root+12)%12),notes[i]);}}}
 void setSelectedPc(int pc){if(pc==selectedPc)return;selectedPc=pc;for(auto& note:notes)note.repaint();}
 void refreshHistory(){const int seq=proc.historySeq.load();if(selSeq>=seq)selSeq=seq-1;if(selSeq<0&&seq>0)selSeq=seq-1;if(selSeq>=0&&seq-selSeq>16)selSeq=seq-1;historyPanel.refresh(proc,selSeq);setSelectedPc(historyPanel.actionPc);}
 void openHistory(){if(showWhy)return;showWhy=true;selSeq=proc.historySeq.load()-1;why.setButtonText("Close");why.setVisible(false);resized();
  refreshHistory();historyPanel.setVisible(true);historyPanel.toFront(false);historyPanel.grabKeyboardFocus();repaint();}
 void closeHistory(){if(!showWhy)return;showWhy=false;selSeq=-1;setSelectedPc(-1);historyPanel.setVisible(false);
  why.setButtonText("Why this note?");why.setVisible(true);resized();why.grabKeyboardFocus();repaint();}
 void setTargetEnabled(int pc,bool enable){const int root=(int)proc.value("key");const int degree=((pc-root)%12+12)%12;
  auto* parameter=proc.state.getParameter("note"+juce::String(degree));if(parameter==nullptr)return;
  if((parameter->getValue()>.5f)==enable)return;
  edit([&]{parameter->beginChangeGesture();parameter->setValueNotifyingHost(enable?1.f:0.f);parameter->endChangeGesture();});
  refreshHistory();repaint();}
 void timerCallback()override{animationTime+=1.f/30.f;smoothedLevel+=(juce::jlimit(0.f,1.f,proc.level.load()*2)-smoothedLevel)*.18f;
 const float detected=proc.pitch.load();const int target=megatune::currentTargetNote(proc);
 const int ip=detected>0?(((int)std::lround(detected))%12+12)%12:-1;
 const int op=target>0?((target%12)+12)%12:-1;const int root=(int)proc.value("key");
 for(int i=0;i<12;++i){notes[i].scaleDot=proc.noteValue(((i-root)+24)%12)>.5f;notes[i].targetFill=i==op;notes[i].inputOutline=i==ip;notes[i].selectedKey=i==selectedPc;notes[i].repaint();}
 if(!juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())remember();
 if(showWhy)refreshHistory();
 const int wantEngine=proc.value("smith")>.5f?4:proc.value("engine")>.5f?3:proc.value("lowlatency")>.5f?2:1;
 if(wantEngine!=shownEngine){shownEngine=wantEngine;enginePick.setSelectedId(wantEngine,juce::dontSendNotification);}
 experimental.setVisible(wantEngine==3||wantEngine==4);
 undo.setEnabled(!past.empty());redo.setEnabled(!future.empty());slotA.setToggleState(activeSlot==0,juce::dontSendNotification);slotB.setToggleState(activeSlot==1,juce::dontSendNotification);
 const int program=proc.getCurrentProgram();if(program!=shownProgram){shownProgram=program;presets.setSelectedId(program+1,juce::dontSendNotification);}
 syncKey();repaint(31,135,(showWhy?844:1419),220);repaint(0,1028,1480,32);}
 void setScale(bool isMinor,bool all){const int scaleMajor[]={0,2,4,5,7,9,11};const int scaleMinor[]={0,2,3,5,7,8,10};for(int i=0;i<12;++i){bool selected=all;for(int j=0;j<7;++j)if(i==(isMinor?scaleMinor[j]:scaleMajor[j]))selected=true;auto* p=proc.state.getParameter("note"+juce::String(i));p->beginChangeGesture();p->setValueNotifyingHost(selected?1.f:0.f);p->endChangeGesture();}}
public:
 void tick(){timerCallback();}
 Editor(MegaTune& p):AudioProcessorEditor(p),proc(p),presetBrowser(p){setLookAndFeel(&tunerLook);setSize(1480,1060);auto setup=[&](juce::Slider& s,const char* id){addAndMakeVisible(s);s.setLookAndFeel(&tunerLook);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.setNumDecimalPlacesToDisplay(1);s.setDoubleClickReturnValue(true,proc.state.getParameter(id)->convertFrom0to1(proc.state.getParameter(id)->getDefaultValue()));s.setColour(juce::Slider::thumbColourId,juce::Colour(0xffbb8de9));s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,75,22);s.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffc48cea));s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff343137));s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);s.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xfff4f1ff));s.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);auto attachment=std::make_unique<SA>(proc.state,id,s);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.updateText();return attachment;};a=setup(speed,"speed");b=setup(sustain,"sustain");c=setup(amount,"amount");da=setup(doubler,"doubler");wa=setup(width,"width");ra=setup(reference,"reference");sa=setup(shift,"shift");rga=setup(range,"range");for(auto* slider:{&doubler,&width}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,38,20);slider->setTextValueSuffix("%");}reference.setSliderStyle(juce::Slider::LinearHorizontal);reference.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,20);reference.setTextValueSuffix(" Hz");reference.textFromValueFunction=[](double v){return juce::String(v,0);};reference.updateText();doubler.setName("Doubler");width.setName("Width");reference.setName("Reference");shift.setSliderStyle(juce::Slider::LinearHorizontal);shift.setTextBoxStyle(juce::Slider::TextBoxRight,false,46,20);shift.setTextValueSuffix(" st");shift.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};shift.updateText();shift.setName("Shift");range.setSliderStyle(juce::Slider::LinearHorizontal);range.setTextBoxStyle(juce::Slider::TextBoxRight,false,40,20);range.setTextValueSuffix(" st");range.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};range.updateText();range.setName("Range");
 addAndMakeVisible(presets);for(int i=0;i<proc.getNumPrograms();++i)presets.addItem(proc.getProgramName(i),i+1);presets.setSelectedId(proc.getCurrentProgram()+1,juce::dontSendNotification);presets.onChange=[this]{edit([&]{proc.setCurrentProgram(presets.getSelectedId()-1);});};
 addAndMakeVisible(browse);browse.setComponentID("preset-browse");browse.onClick=[this]{presetBrowser.show();};
 addChildComponent(presetBrowser);presetBrowser.setVisible(false);presetBrowser.onLoad=[this](int index){edit([&]{proc.setCurrentProgram(index);});};presetBrowser.onClose=[this]{repaint();};
 for(auto* button:{&previous,&next,&undo,&redo,&slotA,&slotB,&copyAB})addAndMakeVisible(button);
 previous.setTooltip("Previous factory preset");next.setTooltip("Next factory preset");copyAB.setTooltip("Copy the current sound to the other comparison slot");
 previous.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+proc.getNumPrograms()-1)%proc.getNumPrograms());});};next.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+1)%proc.getNumPrograms());});};
 undo.onClick=[this]{remember();if(past.empty())return;future.push_back(capture());auto sound=past.back();past.pop_back();restore(sound);};
 redo.onClick=[this]{if(future.empty())return;past.push_back(capture());auto sound=future.back();future.pop_back();restore(sound);};
 slotA.onClick=[this]{if(activeSlot!=0)compare(0);};slotB.onClick=[this]{if(activeSlot!=1)compare(1);};copyAB.onClick=[this]{comparison[1-activeSlot]=capture();};
 speed.setName("Speed");sustain.setName("Sustain");amount.setName("Amount");speed.textFromValueFunction=[](double v){return juce::String(v,1)+" ms";};sustain.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};amount.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};speed.updateText();sustain.updateText();amount.updateText();speed.setComponentID("control-speed");sustain.setComponentID("control-sustain");amount.setComponentID("control-amount");doubler.setComponentID("control-doubler");width.setComponentID("control-width");reference.setComponentID("control-reference");stabilize.setComponentID("control-stabilize");undo.setComponentID("undo-button");for(auto* v:{&bypass,&stabilize,&lowLatency})addAndMakeVisible(v);d=std::make_unique<BA>(proc.state,"bypass",bypass);e=std::make_unique<BA>(proc.state,"stabilize",stabilize);ll=std::make_unique<BA>(proc.state,"lowlatency",lowLatency);
 const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i){addAndMakeVisible(notes[i]);notes[i].setButtonText(names[i]);notes[i].setClickingTogglesState(true);bool black=i==1||i==3||i==6||i==8||i==10;notes[i].blackKey=black;na[i]=std::make_unique<BA>(proc.state,"note"+juce::String(i),notes[i]);}
 addAndMakeVisible(key);const char* roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i)key.addItem(roots[i],i+1);keyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state,"key",key);key.onChange=[this]{syncKey();repaint();};
 for(auto* v:{&major,&minor,&chromatic})addAndMakeVisible(v);major.onClick=[this]{setScale(false,false);};minor.onClick=[this]{setScale(true,false);};chromatic.onClick=[this]{setScale(false,true);};
 addAndMakeVisible(enginePick);addAndMakeVisible(why);why.setTooltip("Recent pitch decisions");addAndMakeVisible(historyPanel);historyPanel.setVisible(false);historyPanel.setWantsKeyboardFocus(true);
 historyPanel.onClose=[this]{closeHistory();};historyPanel.onSelect=[this](int row){selSeq=proc.historySeq.load()-1-row;refreshHistory();};
 historyPanel.onEnable=[this](int pc){setTargetEnabled(pc,true);};historyPanel.onExclude=[this](int pc){setTargetEnabled(pc,false);};historyPanel.onUndo=[this]{undo.onClick();};
 addAndMakeVisible(experimental);experimental.setComponentID("experimental-label");experimental.setText("EXPERIMENTAL",juce::dontSendNotification);experimental.setFont(10.f);experimental.setColour(juce::Label::textColourId,juce::Colour(0xff9a6bff));experimental.setVisible(false);experimental.setInterceptsMouseClicks(false,false);
 why.setComponentID("why-button");why.onClick=[this]{if(showWhy)closeHistory();else openHistory();};
 enginePick.addItem("Quality",1);enginePick.addItem("Low Latency",2);enginePick.addItem("TD-PSOLA (experimental)",3);enginePick.addItem("Signalsmith (experimental)",4);enginePick.setSelectedId(1,juce::dontSendNotification);enginePick.onChange=[this]{const int id=enginePick.getSelectedId();auto setP=[&](const char* pid,float v){auto* pr=proc.state.getParameter(pid);pr->beginChangeGesture();pr->setValueNotifyingHost(v);pr->endChangeGesture();};setP("lowlatency",id==2?1.f:0.f);setP("engine",id==3?1.f:0.f);setP("smith",id==4?1.f:0.f);};
 auto styleCombo=[&](juce::ComboBox& c){c.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff1b1826));c.setColour(juce::ComboBox::textColourId,juce::Colour(0xfff4f1ff));c.setColour(juce::ComboBox::arrowColourId,juce::Colour(0xff8d879d));c.setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff2d2939));};
 styleCombo(presets);styleCombo(key);styleCombo(enginePick);
 observed=capture();comparison[0]=comparison[1]=observed;resized();timerCallback();startTimerHz(30);}
 ~Editor()override{stopTimer();for(auto* slider:{&speed,&sustain,&amount,&doubler,&width,&reference,&shift,&range})slider->setLookAndFeel(nullptr);setLookAndFeel(nullptr);}
 void resized()override{
  presets.setBounds(266,20,330,44);browse.setBounds(602,20,70,44);previous.setBounds(680,20,44,44);next.setBounds(730,20,44,44);
  undo.setBounds(830,20,94,44);redo.setBounds(932,20,94,44);
  slotA.setBounds(1034,20,42,44);copyAB.setBounds(1082,20,92,44);slotB.setBounds(1184,20,42,44);
  bypass.setBounds(1230,20,220,44);
  presetBrowser.setBounds(getLocalBounds());
  enginePick.setBounds(135,88,280,42);
  experimental.setBounds(426,100,140,18);
  const int cr=showWhy?875:1450;const int cw=cr-31;const int col=cw/3;const int knobY=400,knob=210;
  speed.setBounds(31+col/2-knob/2,knobY,knob,knob);
  sustain.setBounds(31+col+col/2-knob/2,knobY,knob,knob);
  amount.setBounds(31+2*col+col/2-knob/2,knobY,knob,knob);
  const int slot=(cr-92)/4;
  doubler.setBounds(62,640,slot-24,26);width.setBounds(62+slot,640,slot-24,26);
  stabilize.setBounds(62+2*slot+6,636,150,28);reference.setBounds(62+3*slot+10,640,slot-30,26);
  key.setBounds(110,720,95,40);major.setBounds(250,720,58,40);minor.setBounds(314,720,58,40);chromatic.setBounds(378,720,82,40);
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
 void paint(juce::Graphics& g)override{
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
  juce::Point<float> orb(135,245);const float pulse=1.f+smoothedLevel*.10f,orbR=72.f*pulse;
  juce::ColourGradient halo(accent.withAlpha(.20f),orb.x,orb.y,accent.withAlpha(0.f),orb.x+130,orb.y,true);
  g.setGradientFill(halo);g.fillEllipse(orb.x-130,orb.y-130,260,260);
  for(int i=0;i<4;++i){const float r=orbR+i*13.f;g.setColour(accent.withAlpha(.18f-.035f*i));g.drawEllipse(orb.x-r,orb.y-r,r*2,r*2,1.2f);}
  juce::Path core;const int facets=15;
  for(int i=0;i<facets;++i){const float a=juce::MathConstants<float>::twoPi*i/facets+animationTime*.12f;const float r=38.f+10.f*std::sin(animationTime*1.4f+i*1.7f);const auto pt=juce::Point<float>(orb.x+std::cos(a)*r,orb.y+std::sin(a)*r);if(i==0)core.startNewSubPath(pt.x,pt.y);else core.lineTo(pt.x,pt.y);}
  core.closeSubPath();juce::ColourGradient coreFill(accent,orb.x-36,orb.y-36,accentDeep,orb.x+36,orb.y+36,false);g.setGradientFill(coreFill);g.fillPath(core);
  g.setColour(juce::Colour(0xffe6dcff).withAlpha(.8f));g.drawEllipse(orb.x-52,orb.y-52,104,104,1.f);
  g.setColour(line);g.fillRect(juce::Rectangle<int>(280,165,1,160));
  const float p=proc.pitch.load();const int targetNote=megatune::currentTargetNote(proc);
  const int inputC=(int)(31+cw*.45f),targetC=(int)(31+cw*.76f);
  g.setColour(muted);g.setFont(juce::Font(14.f));g.drawText("INPUT",inputC-110,155,220,20,juce::Justification::centred);g.drawText("TARGET",targetC-110,155,220,20,juce::Justification::centred);
  g.setColour(ink);g.setFont(juce::Font(86.f,juce::Font::bold));g.drawText(megaTuneNoteName(p),inputC-160,175,320,100,juce::Justification::centred);
  g.drawText(targetNote>0?megaTuneNoteName((float)targetNote):juce::String("--"),targetC-160,175,320,100,juce::Justification::centred);
  const int arrowY=222;g.setColour(muted);juce::Path arrow;arrow.startNewSubPath(inputC+115,arrowY);arrow.lineTo(targetC-115,arrowY);arrow.startNewSubPath(targetC-127,arrowY-7);arrow.lineTo(targetC-115,arrowY);arrow.lineTo(targetC-127,arrowY+7);
  g.strokePath(arrow,juce::PathStrokeType(2.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
  juce::String dev;if(p>0&&targetNote>0){const float cents=megaTuneCentsBetween(p,(float)targetNote);dev=(cents>=0?"+":"")+juce::String(cents,1)+" cents";}else dev="waiting for pitch";
  g.setColour(accent);g.setFont(juce::Font(20.f,juce::Font::bold));g.drawText(dev,31+280,268,cw-350,28,juce::Justification::centred);
  g.setColour(muted);g.setFont(juce::Font(12.f));g.drawText("INPUT vs TARGET",31+280,294,cw-350,16,juce::Justification::centred);
  const float mx=(float)(31+300),mw=(float)(cw-390),my=320;
  g.setColour(juce::Colour(0xff2a2637));g.fillRoundedRectangle(mx,my-4,mw,8,4);
  g.setColour(line);g.fillRect(juce::Rectangle<float>(mx+mw*.5f-1,my-10,2,20));
  for(int k=-2;k<=2;++k){const float x=mx+mw*(k+2)/4.f;g.setColour(muted.withAlpha(.55f));g.fillRect(juce::Rectangle<float>(x,my-3,1.f,6.f));}
  if(p>0&&targetNote>0){const float x=mx+mw*megaTuneMeterFraction(megaTuneCentsBetween(p,(float)targetNote));g.setColour(accent.withAlpha(.22f));g.fillRoundedRectangle(x-8,my-12,16,24,8);g.setColour(accent);g.fillRoundedRectangle(x-2,my-9,4,18,2);}
  g.setColour(muted);g.setFont(juce::Font(11.f));g.drawText("-100c",mx-20,my+13,50,14,juce::Justification::centred);g.drawText("0c",mx+mw/2-16,my+13,32,14,juce::Justification::centred);g.drawText("+100c",mx+mw-30,my+13,50,14,juce::Justification::centred);
  card(31,370,(float)cw,300.f,14.f);
  const int col=cw/3;const int slot=(cr-92)/4;
  for(int i=0;i<3;++i)if(i>0)g.fillRect(31+col*i,390,1,250);
  g.setColour(muted);g.setFont(juce::Font(13.f));
  const char* names[]={"SPEED","SUSTAIN","AMOUNT"};for(int i=0;i<3;++i)g.drawText(names[i],31+col*i,378,col,18,juce::Justification::centred);
  g.setFont(juce::Font(12.f));
  g.drawText("DOUBLER",62,620,110,16,juce::Justification::left);g.drawText("STEREO WIDTH",62+slot,620,140,16,juce::Justification::left);
  g.drawText("NOTE STABILIZER",62+2*slot+6,620,170,16,juce::Justification::left);g.drawText("PITCH REFERENCE",62+3*slot+10,620,150,16,juce::Justification::left);
  g.setColour(muted);g.setFont(juce::Font(12.f));
  g.drawText("KEY",110,700,95,16,juce::Justification::left);g.drawText("SCALE",250,700,210,16,juce::Justification::left);
  const int shiftX=(int)(cr*.66f-60),rangeX=(int)(cr*.80f-60);
  g.drawText("SHIFT",shiftX,700,120,16,juce::Justification::left);g.drawText("RANGE",rangeX,700,120,16,juce::Justification::left);
  card(31,790,(float)cw,210.f,14.f);
  // footer
  g.setColour(line);g.fillRect(0,1028,1480,1);
  g.setColour(muted);g.setFont(juce::Font(11.f));g.drawText("ENGINE",40,1038,56,18,juce::Justification::left);
  g.setColour(ink);g.setFont(juce::Font(16.f,juce::Font::bold));g.drawText(proc.value("smith")>.5f?"Signalsmith":proc.value("engine")>.5f?"TD-PSOLA":proc.value("lowlatency")>.5f?"Low Latency":"Quality",104,1036,170,22,juce::Justification::left);
  g.setColour(line);g.fillRect(280,1038,1,18);
  g.setColour(muted);g.setFont(juce::Font(14.f));g.drawText("MegaTune v"+juce::String(JucePlugin_VersionString),300,1036,220,22,juce::Justification::left);
  g.setColour(line);g.fillRect(520,1038,1,18);
  const int root=(int)proc.value("key");const char* rootNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  const int legY=1046;
  g.setColour(accent);g.fillRoundedRectangle(650,1038,18,18,5);g.setColour(juce::Colour(0xff17121f));g.setFont(juce::Font(12.f,juce::Font::bold));g.drawText("✓",650,1038,18,18,juce::Justification::centred);
  g.setColour(ink);g.setFont(juce::Font(13.f));g.drawText("Enabled",676,1038,80,18,juce::Justification::left);
  g.setColour(juce::Colour(0xff8ee7ff));g.drawRoundedRectangle(782,1040,16,14,4,2.f);g.setColour(ink);g.drawText("Input",806,1038,60,18,juce::Justification::left);
  g.setColour(accent);g.fillRoundedRectangle(884,1039,18,16,5);g.setColour(ink);g.drawText("Target",910,1038,70,18,juce::Justification::left);
  int scaleMask=0;for(int n=0;n<12;++n)if(proc.noteValue(n)>.5f)scaleMask|=1<<n;
  const juce::String scaleName=scaleMask==4095?"Chromatic":(scaleMask==2741?"Major":(scaleMask==1453?"Minor":"Custom"));
  g.setColour(accent);g.fillEllipse(995,1042,9,9);g.setColour(ink);g.drawText("Scale note ("+juce::String(rootNames[root])+" "+scaleName+")",1012,1038,240,18,juce::Justification::left);
  g.setColour(line);g.fillRect(1250,1038,1,18);
  g.setColour(accent);for(int i=0;i<4;++i){const float h=5.f+i*4.f;g.fillRoundedRectangle(1272+i*5,1049-h,3,h,1);}
  g.setColour(ink);g.setFont(juce::Font(14.f));g.drawText(p>0?"Audio: Active":"Audio: Idle",1294,1036,140,22,juce::Justification::left);
 } };
namespace megatune {
int historySequence(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->historySeq.load();return 0;}
DecisionView historyDecision(const juce::AudioProcessor& processor,int sequence){if(auto* tune=dynamic_cast<const MegaTune*>(&processor)){const auto& d=tune->decisions[(size_t)(sequence&15)];return {d.in,d.target,d.reason};}return {};}
void appendDecision(juce::AudioProcessor& processor,float in,float target,int reason){if(auto* tune=dynamic_cast<MegaTune*>(&processor))tune->recordDecision(in,target,reason);}
int currentTargetNote(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->currentTargetNote.load();return 0;}
int smithBufferCapacity(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->smithBufferCapacity();return 0;}
void tickEditor(juce::AudioProcessorEditor& editor){if(auto* view=dynamic_cast<Editor*>(&editor))view->tick();}
}
juce::AudioProcessorEditor* MegaTune::createEditor(){return new Editor(*this);}juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MegaTune();}
