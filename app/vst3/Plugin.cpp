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
 void prepareToPlay(double r,int)override {rate=r;write=0;analysisCount=0;phase=0;ratio=1;target=0;lastNote=0;heldSeconds=0;doublePhase=0;currentTargetNote=0;for(auto& c:doubleRing)c.fill(0); for(auto& c:ring)c.fill(0); history.fill(0);hist=0;lowLatencyActive=value("lowlatency")>.5f;window=lowLatencyActive?128:2048;setLatencySamples(window);pitchCount=0;psola[0].prepare(window);psola[1].prepare(window);if(!smithReady){smith.presetDefault(2,(float)rate);smithReady=true;}smith.reset();smithLatency=smith.inputLatency()+smith.outputLatency();if(value("smith")>.5f&&!lowLatencyActive)setLatencySamples(smithLatency);sIn[0].assign(8192,0.f);sIn[1].assign(8192,0.f);sOut[0].assign(8192,0.f);sOut[1].assign(8192,0.f);}
 void releaseResources()override{}
 float value(const char* id) const{const char* ids[]={"speed","sustain","amount","bypass","stabilize","key","doubler","width","reference","lowlatency","shift","range","engine","smith","smithcomp"};for(int i=0;i<15;++i)if(std::strcmp(id,ids[i])==0)return raw[i]->load();auto* v=state.getRawParameterValue(id);return v?v->load():0;}
 float noteValue(int i)const{return noteRaw[i]->load();}
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
  const auto yin=megatune::detectYin(x.data(),n,sr,70.f,1000.f);
  if(!yin.voiced){pitch=0;target=0;lastNote=0;heldSeconds=0;pitchCount=0;currentTargetNote=0;recordDecision(0,0,R_UNVOICED);return;}
  float midi=69+12*std::log2(yin.hz/juce::jmax(1.f,value("reference")));pitch=midi;
  if(pitchCount<3)pitchHist[pitchCount++]=midi;else{pitchHist[0]=pitchHist[1];pitchHist[1]=pitchHist[2];pitchHist[2]=midi;const float lo=std::min(pitchHist[0],std::min(pitchHist[1],pitchHist[2])),hi=std::max(pitchHist[0],std::max(pitchHist[1],pitchHist[2]));midi=pitchHist[0]+pitchHist[1]+pitchHist[2]-lo-hi;}
  float distance=100;int nearest=0;for(int i=(int)std::floor(midi)-12;i<=(int)std::ceil(midi)+12;++i)if(noteValue((i-(int)value("key")+24)%12)>.5f&&std::abs(i-midi)<distance){distance=std::abs(i-midi);nearest=i;}
  if(distance==100){target=0;lastNote=0;heldSeconds=0;currentTargetNote=0;recordDecision(midi,0,R_NONOTE);return;}if(value("stabilize")>.5f&&lastNote>0&&std::abs(midi-lastNote)<.6f&&noteValue((lastNote-(int)value("key")+24)%12)>.5f)nearest=lastNote;{unsigned m=0;for(int q=0;q<12;++q)if(noteRaw[q]->load()>.5f)m|=1u<<q;megatune::debugMask.store(m);megatune::debugNearest.store(nearest);megatune::debugMidi.store((int)std::lround(midi*100.f));megatune::debugKey.store((int)value("key"));}if(nearest!=lastNote)heldSeconds=0;const bool stabilizeOn=value("stabilize")>.5f;lastNote=nearest;currentTargetNote=nearest;const bool held=(lastNote==nearest&&nearest>0&&stabilizeOn);const float correction=(float)(nearest-midi);const bool outOfRange=std::abs(correction)>value("range");
  target=(outOfRange?0.f:correction)*value("amount")*.01f+value("shift");
  recordDecision(midi,(float)nearest,outOfRange?R_RANGE:(held?R_HELD:R_NEAREST));psolaPeriod=(float)(rate/juce::jmax(1.f,yin.hz));const float psRatio=std::pow(2.f,target/12);psola[0].setTarget(psolaPeriod,psRatio,yin.voiced);psola[1].setTarget(psolaPeriod,psRatio,yin.voiced);if(yin.voiced) voicedHold=(int)(rate*0.12);
 }
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{processAudio(b,false);}
 void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{processAudio(b,true);}
 void processAudio(juce::AudioBuffer<float>& b,bool hostBypass){
  if(!hostBypass&&value("smith")>.5f&&!lowLatencyActive&&value("amount")>0.f){const int n=b.getNumSamples();
   const int channels=b.getNumChannels();
   for(int i=0;i<n;++i){float mono=0;for(int c=0;c<channels;++c){const float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0.f;mono+=ring[c][write]/channels;}
    history[hist]=mono;hist=(hist+1)%16384;if(++analysisCount>=512){analysisCount=0;analyse();}write=(write+1)%16384;
    sIn[0][i]=b.getSample(0,i);sIn[1][i]=b.getSample(juce::jmin(1,channels-1),i);}
   smith.setTransposeSemitones((float)target,0.f);
   smith.setFormantSemitones(0.f,value("smithcomp")>.5f);
   const float* ip[2]={sIn[0].data(),sIn[1].data()};float* op[2]={sOut[0].data(),sOut[1].data()};
   smith.process(ip,n,op,n);
   for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<n;++i)b.setSample(c,i,sOut[juce::jmin(1,c)][i]);
   if(getLatencySamples()!=smithLatency)setLatencySamples(smithLatency);
   return;}


  juce::ScopedNoDenormals no;const int channels=b.getNumChannels();float peak=0,outPeak=0;bool bypass=hostBypass||value("bypass")>.5f;
  const int requestedWindow=value("lowlatency")>.5f?128:2048;if(requestedWindow!=window){window=requestedWindow;lowLatencyActive=window==128;setLatencySamples(window);}const int half=window/2;
  for(int i=0;i<b.getNumSamples();++i){float mono=0;for(int c=0;c<channels;++c){float v=b.getSample(c,i);ring[c][write]=std::isfinite(v)?v:0;mono+=ring[c][write]/channels;peak=juce::jmax(peak,std::abs(v));}history[hist]=mono;hist=(hist+1)%16384;
   if(++analysisCount>=512){analysisCount=0;analyse();}
   if(lastNote>0)heldSeconds=juce::jmin(1.f,heldSeconds+1.f/(float)rate);
   float speed=megaTuneSmoothingSeconds(value("speed"),value("sustain"),heldSeconds);float blend=1-std::exp(-1.0f/(float(rate)*juce::jmax(.001f,speed)));ratio+=(std::pow(2.f,target/12)-ratio)*blend;
   phase+=1-ratio;while(phase<0)phase+=window;while(phase>=window)phase-=window;
   for(int c=0;c<channels;++c){auto read=[&](float delay){float pos=write-delay;while(pos<0)pos+=16384;int a=(int)pos;float t=pos-a;
     auto tap=[&](int k){return ring[c][(a+k+16384)%16384];};float y0=tap(-1),y1=tap(0),y2=tap(1),y3=tap(2);
     return y1+.5f*t*(y2-y0+t*(2.f*y0-5.f*y1+4.f*y2-y3+t*(3.f*(y1-y2)+y3-y0)));};float out;
    const float ps=psola[c].process(ring[c][write]);
    if(voicedHold>0)--voicedHold;psolaVoiced=voicedHold>0;psolaGain+=((psolaVoiced?1.f:0.f)-psolaGain)*(psolaVoiced?0.002f:0.0002f);
    if(bypass||value("amount")==0)out=read(window);
    else if(!lowLatencyActive&&value("engine")>.5f)out=psolaGain*ps+(1.f-psolaGain)*read(window);
    else if(lowLatencyActive){float p2=std::fmod(phase+half,(float)window);float w=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*phase/window);out=read(half+phase)*w+read(half+p2)*(1-w);}
    else{const float base=(float)(half+window/4);float acc=0;for(int k=0;k<4;++k){float g=std::fmod(phase+k*window*.25f,(float)window);float wt=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*g/window);acc+=wt*read(base+g);}out=acc*.5f;}
    doubleRing[c][write]=out;
    if(!bypass){float wet=value("doubler")*.005f;float width=value("width")*.01f;float delay=(18.f+(c==0?-6.f:6.f)*width+std::sin(doublePhase+(c==0?0.f:1.7f))*1.2f)*(float)rate*.001f;delay=juce::jlimit(1.f,16000.f,delay);float pos=write-delay;while(pos<0)pos+=16384;int index=(int)pos;float fraction=pos-index;float doubled=doubleRing[c][index]*(1-fraction)+doubleRing[c][(index+1)%16384]*fraction;out=out*(1-wet)+doubled*wet;}
    outPeak=juce::jmax(outPeak,std::abs(out));b.setSample(c,i,out);}
   doublePhase+=1.7f/(float)rate;if(doublePhase>juce::MathConstants<float>::twoPi)doublePhase-=juce::MathConstants<float>::twoPi;write=(write+1)%16384;
  }level=peak;outputLevel=outPeak;float detected=pitch.load();outputPitch=detected>0?detected+((bypass||value("amount")==0)?0.f:12.f*std::log2(juce::jmax(.001f,ratio))):0.f;
 }
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override;
 void getStateInformation(juce::MemoryBlock& m)override{auto saved=state.copyState();saved.setProperty("factoryProgram",currentProgram.load(),nullptr);auto xml=saved.createXml();copyXmlToBinary(*xml,m);}
 void setStateInformation(const void* d,int n)override{auto xml=getXmlFromBinary(d,n);if(xml&&xml->hasTagName(state.state.getType())){auto saved=juce::ValueTree::fromXml(*xml);currentProgram=juce::jlimit(0,getNumPrograms()-1,(int)saved.getProperty("factoryProgram",0));state.replaceState(saved);}}
private:
 std::array<std::atomic<float>*,15> raw{};std::array<std::atomic<float>*,12> noteRaw{};
 double rate=44100;int write=0,hist=0,analysisCount=0,lastNote=0,window=2048,pitchCount=0;float pitchHist[3]{};megatune::Psola psola[2];float psolaPeriod=200.f,psolaGain=0.f;bool psolaVoiced=false;int voicedHold=0;signalsmith::stretch::SignalsmithStretch<float> smith;bool smithReady=false;std::vector<float> sIn[2],sOut[2];int smithLatency=0;bool lowLatencyActive=false;float phase=0,ratio=1,target=0,heldSeconds=0;
 std::array<std::array<float,16384>,2> ring{};std::array<float,16384> history{};std::array<std::array<float,16384>,2> doubleRing{};float doublePhase=0;
};
class TunerLook final:public juce::LookAndFeel_V4 {
public:
 void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,float position,float,float,juce::Slider& slider)override{
 auto area=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height).reduced(5);float radius=juce::jmin(area.getWidth(),area.getHeight())*.5f;auto center=area.getCentre();
 juce::ColourGradient body(juce::Colour(0xff38353b),center.x-radius,center.y-radius,juce::Colour(0xff151418),center.x+radius,center.y+radius,false);g.setGradientFill(body);g.fillEllipse(center.x-radius,center.y-radius,2*radius,2*radius);g.setColour(juce::Colour(0xff111014));g.drawEllipse(center.x-radius,center.y-radius,2*radius,2*radius,2);
 g.setColour(juce::Colour(0xff222126));g.fillEllipse(center.x-radius*.77f,center.y-radius*.77f,radius*1.54f,radius*1.54f);
 const float start=juce::MathConstants<float>::pi*1.2f,sweep=juce::MathConstants<float>::pi*1.6f;
 for(int i=0;i<88;++i){float angle=start+sweep*i/87.f;g.setColour(i<position*88?juce::Colour::fromRGB((juce::uint8)(188+i*.5f),135,(juce::uint8)(167+i*.7f)):juce::Colour(0xff38343d));g.drawLine(center.x+std::sin(angle)*radius*.82f,center.y-std::cos(angle)*radius*.82f,center.x+std::sin(angle)*radius*.94f,center.y-std::cos(angle)*radius*.94f,1.5f);}
 g.setColour(juce::Colour(0xffd1ccd5));g.setFont(11.f);g.drawText(slider.getName(),juce::Rectangle<float>(center.x-radius,center.y+6,radius*2,18),juce::Justification::centred);
 }
 juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider)override{if(!slider.isRotary())return juce::LookAndFeel_V4::getSliderLayout(slider);juce::Slider::SliderLayout layout;layout.sliderBounds=slider.getLocalBounds();layout.textBoxBounds=juce::Rectangle<int>(slider.getWidth()/2-37,slider.getHeight()/2-18,75,24);return layout;}
};
class KeyButton final:public juce::TextButton {
public:
 std::function<void(juce::Graphics&,juce::Rectangle<float>)> overlay;
 void paintButton(juce::Graphics& g,bool,bool)override{juce::TextButton::paintButton(g,false,false);if(overlay)overlay(g,getLocalBounds().toFloat());}
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
  if(selected||over||down){g.setColour(selected?juce::Colour(0xff33294f):juce::Colour(0xff23232a));g.fillRoundedRectangle(r,4);}
  if(selected){g.setColour(juce::Colour(0xff9a6bff));g.fillRoundedRectangle(r.withWidth(3.f),2.f);}
  static const char* rn[]={"nearest enabled note","previous target held","outside Range","unvoiced","no enabled note"};
  g.setColour(juce::Colour(0xffc9c6d4));g.setFont(12.f);
  g.drawText((input>0?megaTuneNoteName(input):juce::String("--"))+"  >  "+(target>0?megaTuneNoteName(target):juce::String("--")),r.withTrimmedLeft(6),juce::Justification::left);
  g.setColour(juce::Colour(0xff9a6bff));g.drawText(rn[juce::jlimit(0,4,reason)],r.withTrimmedLeft(180),juce::Justification::left);
 }
};
class HistoryPanel final:public juce::Component {
public:
 HistoryPanel(){
  setComponentID("history-panel");setOpaque(true);
  for(int i=0;i<6;++i){rows[(size_t)i]=std::make_unique<HistoryRow>(i);addChildComponent(*rows[(size_t)i]);rows[(size_t)i]->onClick=[this,i]{if(onSelect)onSelect(i);};}
  addAndMakeVisible(close);close.setComponentID("history-close");close.onClick=[this]{if(onClose)onClose();};
  addAndMakeVisible(enable);enable.setComponentID("history-enable");enable.onClick=[this]{if(onEnable&&actionPc>=0)onEnable(actionPc);};
  addAndMakeVisible(exclude);exclude.setComponentID("history-exclude");exclude.onClick=[this]{if(onExclude&&actionPc>=0)onExclude(actionPc);};
  addAndMakeVisible(status);status.setComponentID("history-status");status.setColour(juce::Label::textColourId,juce::Colour(0xffb9b5c6));status.setFont(12.f);
 }
 void refresh(const MegaTune& proc,int selectedSeq){
  const int seq=proc.historySeq.load();
  actionPc=-1;bool currentlyEnabled=false;
  for(int i=0;i<6;++i){auto& row=*rows[(size_t)i];const int s=seq-1-i;
   if(s>=0&&s<seq){const auto& d=proc.decisions[(size_t)(s&15)];row.input=d.in;row.target=d.target;row.reason=d.reason;row.selected=s==selectedSeq;row.setVisible(true);}
   else{row.selected=false;row.setVisible(false);}
   row.repaint();}
  if(selectedSeq>=0&&selectedSeq<seq){const auto& d=proc.decisions[(size_t)(selectedSeq&15)];if(d.target>0){actionPc=(((int)std::lround(d.target))%12+12)%12;const int root=(int)proc.value("key");const int degree=((actionPc-root)%12+12)%12;currentlyEnabled=proc.noteValue(degree)>.5f;}}
  const juce::String name=actionPc>=0?megaTunePitchName(actionPc):juce::String();
  enable.setButtonText(actionPc>=0?"Enable "+name:juce::String("Enable"));exclude.setButtonText(actionPc>=0?"Exclude "+name:juce::String("Exclude"));
  enable.setEnabled(actionPc>=0&&!currentlyEnabled);exclude.setEnabled(actionPc>=0&&currentlyEnabled);
  status.setText(actionPc>=0?"Selected target: "+name:"No target for this decision",juce::dontSendNotification);
 }
 void resized()override{
  close.setBounds(getWidth()-76,8,64,22);
  for(int i=0;i<6;++i)rows[(size_t)i]->setBounds(12,50+i*22,getWidth()-24,19);
  enable.setBounds(12,184,130,22);exclude.setBounds(150,184,130,22);status.setBounds(292,184,getWidth()-304,22);
 }
 void paint(juce::Graphics& g)override{
  g.fillAll(juce::Colour(0xff141418));
  g.setColour(juce::Colour(0xff2e2b38));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),6.f,1.f);
  g.setColour(juce::Colour(0xffefeef5));g.setFont(14.f);g.drawText("Recent pitch decisions",12,8,300,18,juce::Justification::left);
  g.setColour(juce::Colour(0xff8d8a99));g.setFont(10.f);g.drawText("history is diagnostic; every edit goes through Undo",12,26,500,14,juce::Justification::left);
  g.setColour(juce::Colour(0xff2a2a31));g.fillRect(12,44,getWidth()-24,1);
 }
 int actionPc=-1;
 std::function<void(int)> onSelect,onEnable,onExclude;std::function<void()> onClose;
private:
 std::array<std::unique_ptr<HistoryRow>,6> rows;
 juce::TextButton close{"Close"},enable{"Enable"},exclude{"Exclude"};juce::Label status;
};
class Editor final:public juce::AudioProcessorEditor,private juce::Timer {
 TunerLook tunerLook;MegaTune& proc;juce::Slider speed,sustain,amount,doubler,width,reference,shift,range;juce::ComboBox presets;juce::ToggleButton bypass{"Global Bypass"},stabilize{"Note Stabilizer"},lowLatency{"Low Latency"};juce::ComboBox key;int displayedKey=-1;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment;juce::TextButton major{"Major"},minor{"Minor"},chromatic{"Chromatic"};
 struct Sound {std::vector<float> values;int program=0;};
 juce::TextButton previous{"<"},next{">"},undo{"Undo"},redo{"Redo"},slotA{"A"},slotB{"B"},copyAB{"A > B"};juce::TextButton why{"Why this note?"};bool showWhy=false;int selSeq=-1,selectedPc=-1;HistoryPanel historyPanel;juce::Label experimental;juce::ComboBox enginePick;int shownEngine=-1;
 Sound comparison[2],observed;std::vector<Sound> past,future;int activeSlot=0;
 float animationTime=0,smoothedLevel=0;std::array<float,12> inputGlow{},outputGlow{};
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
 void openHistory(){if(showWhy)return;showWhy=true;selSeq=proc.historySeq.load()-1;why.setButtonText("Close");
  for(auto* control:{&speed,&sustain,&amount,&doubler,&width,&reference})control->setVisible(false);stabilize.setVisible(false);
  refreshHistory();historyPanel.setVisible(true);historyPanel.toFront(false);historyPanel.grabKeyboardFocus();repaint();}
 void closeHistory(){if(!showWhy)return;showWhy=false;selSeq=-1;setSelectedPc(-1);historyPanel.setVisible(false);
  for(auto* control:{&speed,&sustain,&amount,&doubler,&width,&reference})control->setVisible(true);stabilize.setVisible(true);
  why.setButtonText("Why this note?");why.grabKeyboardFocus();repaint();}
 void setTargetEnabled(int pc,bool enable){const int root=(int)proc.value("key");const int degree=((pc-root)%12+12)%12;
  auto* parameter=proc.state.getParameter("note"+juce::String(degree));if(parameter==nullptr)return;
  if((parameter->getValue()>.5f)==enable)return;
  edit([&]{parameter->beginChangeGesture();parameter->setValueNotifyingHost(enable?1.f:0.f);parameter->endChangeGesture();});
  refreshHistory();repaint();}
 void timerCallback()override{animationTime+=1.f/30.f;smoothedLevel+=(juce::jlimit(0.f,1.f,proc.level.load()*2)-smoothedLevel)*.18f;
 for(int i=0;i<12;++i){inputGlow[i]*=.89f;outputGlow[i]*=.89f;}float detected=proc.pitch.load();if(detected>0&&smoothedLevel>.001f){int note=((int)std::round(detected)%12+12)%12;inputGlow[note]=smoothedLevel;float actual=proc.outputPitch.load();if(actual>0){int targetNote=((int)std::round(actual)%12+12)%12;outputGlow[targetNote]=smoothedLevel;}}
 if(!juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())remember();
 if(showWhy)refreshHistory();
 const int wantEngine=proc.value("smith")>.5f?4:proc.value("engine")>.5f?3:proc.value("lowlatency")>.5f?2:1;
 if(wantEngine!=shownEngine){shownEngine=wantEngine;enginePick.setSelectedId(wantEngine,juce::dontSendNotification);}
 experimental.setVisible(wantEngine==3||wantEngine==4);
 undo.setEnabled(!past.empty());redo.setEnabled(!future.empty());slotA.setToggleState(activeSlot==0,juce::dontSendNotification);slotB.setToggleState(activeSlot==1,juce::dontSendNotification);presets.setSelectedId(proc.getCurrentProgram()+1,juce::dontSendNotification);syncKey();repaint();}
 void setScale(bool isMinor,bool all){const int scaleMajor[]={0,2,4,5,7,9,11};const int scaleMinor[]={0,2,3,5,7,8,10};for(int i=0;i<12;++i){bool selected=all;for(int j=0;j<7;++j)if(i==(isMinor?scaleMinor[j]:scaleMajor[j]))selected=true;auto* p=proc.state.getParameter("note"+juce::String(i));p->beginChangeGesture();p->setValueNotifyingHost(selected?1.f:0.f);p->endChangeGesture();}}
public:
 void tick(){timerCallback();}
 Editor(MegaTune& p):AudioProcessorEditor(p),proc(p){setSize(860,680);auto setup=[&](juce::Slider& s,const char* id){addAndMakeVisible(s);s.setLookAndFeel(&tunerLook);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.setNumDecimalPlacesToDisplay(1);s.setDoubleClickReturnValue(true,proc.state.getParameter(id)->convertFrom0to1(proc.state.getParameter(id)->getDefaultValue()));s.setColour(juce::Slider::thumbColourId,juce::Colour(0xffbb8de9));s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,75,22);s.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffc48cea));s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff343137));s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);auto attachment=std::make_unique<SA>(proc.state,id,s);s.textFromValueFunction=[](double value){return juce::String(value,1);};s.updateText();return attachment;};a=setup(speed,"speed");b=setup(sustain,"sustain");c=setup(amount,"amount");da=setup(doubler,"doubler");wa=setup(width,"width");ra=setup(reference,"reference");sa=setup(shift,"shift");rga=setup(range,"range");for(auto* slider:{&doubler,&width}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,38,20);slider->setTextValueSuffix("%");}reference.setSliderStyle(juce::Slider::LinearHorizontal);reference.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,20);reference.setTextValueSuffix(" Hz");reference.textFromValueFunction=[](double v){return juce::String(v,0);};reference.updateText();doubler.setName("Doubler");width.setName("Width");reference.setName("Reference");shift.setSliderStyle(juce::Slider::LinearHorizontal);shift.setTextBoxStyle(juce::Slider::TextBoxRight,false,46,20);shift.setTextValueSuffix(" st");shift.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};shift.updateText();shift.setName("Shift");range.setSliderStyle(juce::Slider::LinearHorizontal);range.setTextBoxStyle(juce::Slider::TextBoxRight,false,40,20);range.setTextValueSuffix(" st");range.textFromValueFunction=[](double v){return juce::String((int)std::lround(v));};range.updateText();range.setName("Range");
 addAndMakeVisible(presets);for(int i=0;i<proc.getNumPrograms();++i)presets.addItem(proc.getProgramName(i),i+1);presets.setSelectedId(proc.getCurrentProgram()+1,juce::dontSendNotification);presets.onChange=[this]{edit([&]{proc.setCurrentProgram(presets.getSelectedId()-1);});};
 for(auto* button:{&previous,&next,&undo,&redo,&slotA,&slotB,&copyAB})addAndMakeVisible(button);
 previous.setTooltip("Previous factory preset");next.setTooltip("Next factory preset");copyAB.setTooltip("Copy the current sound to the other comparison slot");
 previous.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+proc.getNumPrograms()-1)%proc.getNumPrograms());});};next.onClick=[this]{edit([&]{proc.setCurrentProgram((proc.getCurrentProgram()+1)%proc.getNumPrograms());});};
 undo.onClick=[this]{remember();if(past.empty())return;future.push_back(capture());auto sound=past.back();past.pop_back();restore(sound);};
 redo.onClick=[this]{if(future.empty())return;past.push_back(capture());auto sound=future.back();future.pop_back();restore(sound);};
 slotA.onClick=[this]{if(activeSlot!=0)compare(0);};slotB.onClick=[this]{if(activeSlot!=1)compare(1);};copyAB.onClick=[this]{comparison[1-activeSlot]=capture();};
 speed.setName("Speed");sustain.setName("Sustain");amount.setName("Amount");speed.setComponentID("control-speed");sustain.setComponentID("control-sustain");amount.setComponentID("control-amount");doubler.setComponentID("control-doubler");width.setComponentID("control-width");reference.setComponentID("control-reference");stabilize.setComponentID("control-stabilize");undo.setComponentID("undo-button");for(auto* v:{&bypass,&stabilize,&lowLatency})addAndMakeVisible(v);d=std::make_unique<BA>(proc.state,"bypass",bypass);e=std::make_unique<BA>(proc.state,"stabilize",stabilize);ll=std::make_unique<BA>(proc.state,"lowlatency",lowLatency);
 const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i){addAndMakeVisible(notes[i]);notes[i].setButtonText(names[i]);notes[i].setClickingTogglesState(true);bool black=i==1||i==3||i==6||i==8||i==10;notes[i].setColour(juce::TextButton::buttonOnColourId,juce::Colour(black?0xff232329:0xff3a3a42));notes[i].setColour(juce::TextButton::buttonColourId,juce::Colour(black?0xff1b1b20:0xff33333a));na[i]=std::make_unique<BA>(proc.state,"note"+juce::String(i),notes[i]);
  notes[i].overlay=[this,i](juce::Graphics& g,juce::Rectangle<float> r){
   const float pv=proc.pitch.load();
   const int ip=pv>0?(((int)std::lround(pv))%12+12)%12:-1;
   const int target=megatune::currentTargetNote(proc);
   const int op=target>0?((target%12)+12)%12:-1;
   if(i==op){g.setColour(juce::Colour(0xff9a6bff).withAlpha(.62f));g.fillRoundedRectangle(r.reduced(1.f),5.f);}
   if(i==ip){g.setColour(juce::Colour(0xffe2d6ff));g.drawRoundedRectangle(r.reduced(2.f),5.f,2.f);}
   const int root2=(int)proc.value("key");if(proc.noteValue(((i-root2)+24)%12)>.5f){g.setColour(juce::Colour(0xffb49aff));g.fillEllipse(r.getCentreX()-3.5f,r.getBottom()-14.f,7.f,7.f);}
   if(i==selectedPc){g.setColour(juce::Colours::white);g.drawRoundedRectangle(r.reduced(4.f),6.f,2.5f);g.fillRect(r.getCentreX()-5.f,r.getY()+3.f,10.f,3.f);}
  };}
 addAndMakeVisible(key);const char* roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};for(int i=0;i<12;++i)key.addItem(roots[i],i+1);keyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state,"key",key);key.onChange=[this]{syncKey();repaint();};
 for(auto* v:{&major,&minor,&chromatic})addAndMakeVisible(v);major.onClick=[this]{setScale(false,false);};minor.onClick=[this]{setScale(true,false);};chromatic.onClick=[this]{setScale(false,true);};
 addAndMakeVisible(enginePick);addAndMakeVisible(why);why.setTooltip("Recent pitch decisions");addAndMakeVisible(historyPanel);historyPanel.setVisible(false);historyPanel.setWantsKeyboardFocus(true);
 historyPanel.onClose=[this]{closeHistory();};historyPanel.onSelect=[this](int row){selSeq=proc.historySeq.load()-1-row;refreshHistory();};
 historyPanel.onEnable=[this](int pc){setTargetEnabled(pc,true);};historyPanel.onExclude=[this](int pc){setTargetEnabled(pc,false);};
 addAndMakeVisible(experimental);experimental.setComponentID("experimental-label");experimental.setText("EXPERIMENTAL",juce::dontSendNotification);experimental.setFont(10.f);experimental.setColour(juce::Label::textColourId,juce::Colour(0xff9a6bff));experimental.setVisible(false);experimental.setInterceptsMouseClicks(false,false);
 why.setComponentID("why-button");why.onClick=[this]{if(showWhy)closeHistory();else openHistory();};
 enginePick.addItem("Quality",1);enginePick.addItem("Low Latency",2);enginePick.addItem("TD-PSOLA (experimental)",3);enginePick.addItem("Signalsmith (experimental)",4);enginePick.setSelectedId(1,juce::dontSendNotification);enginePick.onChange=[this]{const int id=enginePick.getSelectedId();auto setP=[&](const char* pid,float v){auto* pr=proc.state.getParameter(pid);pr->beginChangeGesture();pr->setValueNotifyingHost(v);pr->endChangeGesture();};setP("lowlatency",id==2?1.f:0.f);setP("engine",id==3?1.f:0.f);setP("smith",id==4?1.f:0.f);};observed=capture();comparison[0]=comparison[1]=observed;resized();timerCallback();startTimerHz(30);}
 ~Editor()override{stopTimer();for(auto* slider:{&speed,&sustain,&amount,&doubler,&width,&reference,&shift,&range})slider->setLookAndFeel(nullptr);}
 void resized()override{
  presets.setBounds(112,9,190,26);previous.setBounds(306,9,26,26);next.setBounds(334,9,26,26);
  undo.setBounds(376,9,50,26);redo.setBounds(428,9,50,26);
  slotA.setBounds(500,9,26,26);copyAB.setBounds(528,9,52,26);slotB.setBounds(582,9,26,26);
  bypass.setBounds(700,9,150,26);
  enginePick.setBounds(470,46,170,26);
  speed.setBounds(90,268,150,124);sustain.setBounds(355,268,150,124);amount.setBounds(620,268,150,124);
  doubler.setBounds(62,414,232,22);width.setBounds(318,414,232,22);
  stabilize.setBounds(576,412,140,24);
  reference.setBounds(716,412,124,22);
  key.setBounds(96,472,72,24);major.setBounds(186,472,60,24);minor.setBounds(248,472,60,24);chromatic.setBounds(310,472,84,24);
  shift.setBounds(420,472,80,22);range.setBounds(512,472,80,22);why.setBounds(660,472,170,24);
  experimental.setBounds(646,52,96,14);historyPanel.setBounds(20,246,820,208);
  lowLatency.setBounds(-200,-200,10,10);
  const int white[]={0,2,4,5,7,9,11};
  for(int j2=0;j2<7;++j2)notes[white[j2]].setBounds(40+j2*111,528,109,120);
  const int black[]={1,3,6,8,10};const int gaps[]={1,2,4,5,6};
  for(int j2=0;j2<5;++j2){notes[black[j2]].setBounds(40+gaps[j2]*111-28,528,56,74);notes[black[j2]].toFront(false);}}
 void paint(juce::Graphics& g)override{
  const auto bg=juce::Colour(0xff101013),panel=juce::Colour(0xff19191d),line=juce::Colour(0xff2a2a31),accent=juce::Colour(0xff9a6bff),muted=juce::Colour(0xff8d8a99);
  g.fillAll(bg);
  g.setColour(juce::Colour(0xffe9e7f0));g.setFont(22.f);g.drawText("MegaTune",14,8,96,26,juce::Justification::left);
  g.setColour(line);g.fillRect(juce::Rectangle<int>(0,40,860,1));
  g.setColour(panel);g.fillRoundedRectangle(20.f,86.f,820.f,152.f,6.f);
  g.setColour(line);g.drawRoundedRectangle(20.f,86.f,820.f,152.f,6.f,1.f);
  // orb (small)
  auto c=juce::Point<float>(92.f,162.f);
  for(int i=0;i<3;++i){float r=34.f+i*9.f;g.setColour(accent.withAlpha(.10f+.05f*i));g.drawEllipse(c.x-r,c.y-r,r*2,r*2,1.1f);}
  g.setColour(accent.withAlpha(.16f));g.fillEllipse(c.x-20,c.y-20,40,40);
  g.setColour(line);g.fillRect(juce::Rectangle<int>(168,110,1,104));
  // labels
  g.setColour(muted);g.setFont(11.f);
  g.drawText("INPUT",300,100,130,14,juce::Justification::centred);
  g.drawText("TARGET",560,100,130,14,juce::Justification::centred);
  float p=proc.pitch.load();const int targetNote=megatune::currentTargetNote(proc);
  g.setColour(juce::Colour(0xffefeef5));g.setFont(54.f);
  g.drawText(megaTuneNoteName(p),300,116,130,66,juce::Justification::centred);
  g.setColour(muted);g.setFont(26.f);g.drawText(">",445,126,30,44,juce::Justification::centred);
  g.setColour(juce::Colour(0xffefeef5));g.setFont(54.f);
  g.drawText(targetNote>0?megaTuneNoteName((float)targetNote):juce::String("--"),540,116,150,66,juce::Justification::centred);
  juce::String dev;if(p>0&&targetNote>0){float cents=megaTuneCentsBetween(p,(float)targetNote);dev=(cents>=0?"+":"")+juce::String(cents,1)+juce::String(" cents");}
  else dev="waiting for pitch";
  g.setColour(accent);g.setFont(15.f);g.drawText(dev,300,182,390,20,juce::Justification::centred);
  g.setColour(muted);g.setFont(10.f);g.drawText("Input vs Target",300,198,390,12,juce::Justification::centred);
  // cents meter, negative on the left
  float mx=250.f,mw=420.f,my=210.f;
  g.setColour(line);g.fillRect(juce::Rectangle<float>(mx,my,mw,1.f));
  for(int k=-2;k<=2;++k){float x=mx+mw*(k+2)/4.f;g.fillRect(juce::Rectangle<float>(x,my-4,1.f,8.f));}
  if(p>0&&targetNote>0){float x=mx+mw*megaTuneMeterFraction(megaTuneCentsBetween(p,(float)targetNote));
    g.setColour(accent);g.fillRect(juce::Rectangle<float>(x-2,my-7,4.f,14.f));}
  g.setColour(muted);g.setFont(10.f);
  g.drawText("-100c",mx-14,my+8,50,12,juce::Justification::centred);
  g.drawText("0c",mx+mw/2-14,my+8,28,12,juce::Justification::centred);
  g.drawText("+100c",mx+mw-36,my+8,50,12,juce::Justification::centred);
  // readable labels for the always-visible secondary controls
  g.setColour(juce::Colour(0xffb9b5c6));g.setFont(11.f);
  g.drawText("Shift",420,456,80,14,juce::Justification::centred);
  g.drawText("Range",512,456,80,14,juce::Justification::centred);
  if(!showWhy){
    g.setColour(muted);g.setFont(12.f);
    g.drawText("SPEED",90,250,150,14,juce::Justification::centred);
    g.drawText("SUSTAIN",355,250,150,14,juce::Justification::centred);
    g.drawText("AMOUNT",620,250,150,14,juce::Justification::centred);
    g.drawText("DOUBLER",62,398,120,14,juce::Justification::left);
    g.drawText("STEREO WIDTH",318,398,140,14,juce::Justification::left);
    g.drawText("Pitch Reference",716,394,124,14,juce::Justification::centred);
  }
  int lat=proc.getLatencySamples();
  g.setColour(muted);g.setFont(12.f);
  const double srate=proc.getSampleRate();g.drawText((lat>0&&srate>0)?(juce::String(lat*1000.0/srate,1)+" ms"):juce::String("-- ms"),
             746,52,100,14,juce::Justification::right);
 }
 };
namespace megatune {
int historySequence(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->historySeq.load();return 0;}
DecisionView historyDecision(const juce::AudioProcessor& processor,int sequence){if(auto* tune=dynamic_cast<const MegaTune*>(&processor)){const auto& d=tune->decisions[(size_t)(sequence&15)];return {d.in,d.target,d.reason};}return {};}
void appendDecision(juce::AudioProcessor& processor,float in,float target,int reason){if(auto* tune=dynamic_cast<MegaTune*>(&processor))tune->recordDecision(in,target,reason);}
int currentTargetNote(const juce::AudioProcessor& processor){if(auto* tune=dynamic_cast<const MegaTune*>(&processor))return tune->currentTargetNote.load();return 0;}
void tickEditor(juce::AudioProcessorEditor& editor){if(auto* view=dynamic_cast<Editor*>(&editor))view->tick();}
}
juce::AudioProcessorEditor* MegaTune::createEditor(){return new Editor(*this);}juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MegaTune();}
