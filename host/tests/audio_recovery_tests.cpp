// Focused synthetic tests of the real ring producer/consumer, no guest/device required.
#include "../src/audio.cpp"
namespace ps2host::timeline { void NameThread(const char*) {} }
#ifdef AUDIO_BASELINE_SOURCE
namespace baseline_ps2host { using ps2host::AudioTimingSnapshot; namespace timeline = ps2host::timeline; }
#define ps2host baseline_ps2host
#include AUDIO_BASELINE_SOURCE
#undef ps2host
#endif
#include <random>
#include <stdexcept>
namespace {
int checks=0;
void Check(bool v,const char* s){++checks;if(!v)throw std::runtime_error(s);}
using Clock=std::chrono::steady_clock;
using namespace ps2host;
void Fill(Device& d,unsigned count,float value=0.25f){auto w=d.ring.written.load();auto r=d.ring.read.load();Check(w-r+count<=kRingFrames-4,"fixture bounded");for(unsigned n=0;n<count;++n){d.ring.data[2*((w+n)%kRingFrames)]=value;d.ring.data[2*((w+n)%kRingFrames)+1]=-value;}d.ring.written=w+count;}
void Gap(Device& d,int ms){auto now=Clock::now();d.NoteBatch(now-std::chrono::milliseconds(ms));d.NoteBatch(now);}
void Synthetic(){
 float out[960];
 {Device d;d.NoteBatch(Clock::now());Check(!d.recovery_until_us,"first batch not a stall");Gap(d,249);Check(!d.recovery_until_us,"subthreshold gap unchanged");Gap(d,250);Check(d.recovery_until_us>0,"250 ms gap arms recovery without logging");}
 for(int gap:{760,6703,7548})for(unsigned target:{960u,2880u,12000u})for(unsigned long long offset:{0ull,32760ull,10000000000ull}){
  Device d;d.target_frames=target;d.ring.read=offset;d.ring.written=offset;
  Fill(d,kRingFrames-4);Gap(d,gap);d.phase=.6;d.last[0]=.4f;d.last[1]=-.4f;d.buffering=false;
  auto w=d.ring.written.load();d.Render(out,480);
  Check(d.stats.recovered_frames==kRingFrames-4-target && d.stats.recoveries==1,"only excess trimmed, target retained");
  Check(d.ring.written==w && d.ring.read>=w-target && d.ring.read<=w,"reader owns trim; writer unchanged");
  Check(d.ring.Filled()<=target && d.ring.Filled()>=target-483,"recovered to target-sized queue");
  Check(out[0]==.4f && out[1]==-.4f && std::abs(out[2*95]-.25f)<.002f,"2 ms bounded crossfade");
  Check(d.phase>=0 && d.phase<1,"interpolation phase reset valid");
  for(float v:out)Check(std::isfinite(v) && std::abs(v)<=1,"finite bounded stereo output");
 }
 {Device d;Fill(d,32000);d.Render(out,480);Check(d.stats.recoveries==0,"large queue alone does not arm recovery");}
 {Device d;Fill(d,32000);d.recovery_until_us=1;d.Render(out,480);Check(d.stats.recoveries==0 && !d.recovery_until_us,"expired recovery does not trim");}
 {Device d;Fill(d,3000);Gap(d,1000);d.Render(out,480);Check(d.stats.recoveries==0,"normal queue after gap preserved");}
 {Device d;Gap(d,1000);d.Render(out,480);Check(d.buffering && d.ring.read==0,"empty ring remains buffering");Fill(d,32000);d.Render(out,480);Check(d.stats.recoveries==1 && d.ring.Filled()<2880,"later catch-up burst recovered");}
 {Device d;Gap(d,1000);for(int n=0;n<8;++n){Fill(d,unsigned(32000-d.ring.Filled()));d.Render(out,480);}Check(d.stats.recoveries==8,"repeated bursts during recovery bounded");d.recovery_until_us=1;Fill(d,100);d.Render(out,480);Check(d.stats.recoveries==8,"normal mode resumes after bounded window");}
 {Device d;Fill(d,8000);Gap(d,1000);auto r=d.ring.read.load();d.Render(out,0);Check(d.ring.read==r,"zero output no trim");}
 // Exercise actual AudioFrames, not only the ring fixture, with diagnostics disabled.
 {auto& d=D();d.open=true;std::vector<short> in(64000,1234);AudioFrames(in.data(),0);Check(!d.last_batch.time_since_epoch().count(),"empty batches do not arm recovery");d.last_batch=Clock::now()-std::chrono::seconds(1);AudioFrames(in.data(),32000);d.Render(out,480);Check(d.stats.recoveries==1 && d.ring.Filled()<2880,"actual producer detects stall and actual consumer recovers");d.open=false;}
#ifdef AUDIO_BASELINE_SOURCE
 {Device d;baseline_ps2host::Device b;std::mt19937 rng(0xf9b011);std::vector<float> a(2048),ref(2048);for(int n=0;n<3000;++n){unsigned add=rng()%650;unsigned output=1+rng()%500;auto w=d.ring.written.load();auto room=kRingFrames-4-d.ring.Filled();add=unsigned(std::min<unsigned long long>(add,room));for(unsigned k=0;k<add;++k){float value=float(int(rng()%60001)-30000)/32768;for(int ch=0;ch<2;++ch)d.ring.data[2*((w+k)%kRingFrames)+ch]=b.ring.data[2*((w+k)%kRingFrames)+ch]=ch?-value:value;}d.ring.written=b.ring.written=w+add;d.Render(a.data(),output);b.Render(ref.data(),output);Check(std::memcmp(a.data(),ref.data(),2*output*sizeof(float))==0 && d.ring.read==b.ring.read && d.phase==b.phase,"normal output bit-identical to base");}Check(d.stats.recoveries==0,"normal differential never trims");}
#endif
}
}
int main(int argc,char** argv){try{if(argc>1){
 using namespace ps2host;Check(OpenAudio(),"actual audio device opens");auto& d=D();std::vector<short> in(64000,0);
 auto steady=[&](int batches){for(int n=0;n<batches;++n){AudioFrames(in.data(),288);std::this_thread::sleep_for(std::chrono::milliseconds(6));}};
 steady(100);auto before=GetAudioTimingSnapshot();std::this_thread::sleep_for(std::chrono::milliseconds(800));auto empty=GetAudioTimingSnapshot();AudioFrames(in.data(),32000);std::this_thread::sleep_for(std::chrono::milliseconds(30));auto recovered=GetAudioTimingSnapshot();steady(100);auto after=GetAudioTimingSnapshot();
 Check(before.open && empty.read>before.read && empty.written==before.written,"device consumed during producer interruption");Check(d.stats.recoveries>0,"real device applied recovery");Check((recovered.written-recovered.read)<4800 && (after.written-after.read)<5760,"device queue below 100 ms after 30 ms and below 120 ms later");
 std::printf("WASAPI synthetic interruption: before=%.3f dry=%.3f recovered30ms=%.3f final=%.3f ms, trims=%u obsolete=%llu frames\n",double(before.written-before.read)/48,double(empty.written-empty.read)/48,double(recovered.written-recovered.read)/48,double(after.written-after.read)/48,d.stats.recoveries.load(),d.stats.recovered_frames.load());CloseAudio();
 }else Synthetic();std::printf("Audio recovery synthetic: PASS (%d checks)\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
