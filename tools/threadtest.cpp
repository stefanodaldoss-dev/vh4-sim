#include "../app/AmpProcessor.h"
#include <thread>
#include <chrono>
#include <cstdio>
int main(){
  AmpProcessor ap; ap.prepare(48000,4); std::atomic<bool> run{true}; long blocks=0; bool bad=false;
  std::thread audio([&]{ std::vector<float> in(64),out(64); long k=0; while(run){ for(int i=0;i<64;i++) in[i]=0.3f*std::sin(0.05*(k++)); ap.process(in.data(),out.data(),64); for(float v:out) if(!std::isfinite(v)||std::fabs(v)>1.0001f) bad=true; blocks++; std::this_thread::sleep_for(std::chrono::microseconds(200)); } });
  for(int i=0;i<400;i++){ vh4::AmpParams p; p.channel=1+i%4; p.gain=(i%10)/10.0; p.volume=(i%7)/7.0; ap.setParams(p); if(i%97==50) ap.prepare(48000,(i&1)?2:4); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  run=false; audio.join(); printf("blocchi %ld, anomalie: %s\n",blocks,bad?"SI":"nessuna");
}
