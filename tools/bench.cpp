#include "../dsp/vh4dsp.hpp"
#include "wavio.hpp"
#include <chrono>
using namespace vh4;
int main(){
  std::vector<double> x; int sr; readWav("in_6s.wav",x,sr);
  int osf=4; double fs=sr*osf; size_t n=x.size()*osf; std::vector<double> u(n);
  for(size_t i=0;i<n;i++) u[i]=x[i/osf];
  auto T=[&](const char*name,auto f){auto t0=std::chrono::steady_clock::now(); f(); double d=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(); printf("%-18s %.2f s  (%.2fx tempo reale a osf4)\n",name,d,d/(x.size()/double(sr)));};
  Preamp pre; pre.prepare(fs); pre.tagIds(); PreampControls c; pre.setControls(c);
  std::vector<double> y(n);
  T("preamp intero",[&]{for(size_t i=0;i<n;i++) y[i]=pre.process(u[i]);});
  for(int i=0;i<10;i++) if(g_n[i]) printf("stadio %d: %.2f iterazioni/campione\n",i,double(g_it[i])/g_n[i]);
  for(auto&v:y) v*=Amp::kDiv*Amp::kCal*taperLog(0.5);
  PowerAmp pa; pa.prepare(fs); std::vector<double> z(n);
  T("power amp",[&]{for(size_t i=0;i<n;i++) z[i]=pa.process(y[i]);});
  // stadi singoli
  Stage s; s.set(kB,120e3,680,1e-6,4.7e-9,1e6,1e6,100e3,fs,0); s.settle(0);
  T("1 stadio triodo",[&]{double a=0;for(size_t i=0;i<n;i++) a+=s.step(u[i]*0.05);printf("(%g)\n",a);});
  LinNet ln; ln.clear(12); for(int i=0;i<12;i++) ln.R(i,-1,1e5); ln.b[0]=1e-5; ln.node=3; ln.discretize(fs);
  T("rete 12 nodi",[&]{double a=0;for(size_t i=0;i<n;i++) a+=ln.step(u[i]);printf("(%g)\n",a);});
}
