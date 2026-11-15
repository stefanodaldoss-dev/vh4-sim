#include "../dsp/vh4dsp.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vh4;
int main(){
  int bad=0, runs=0; double worst=0; srand(3);
  for(int osf: {4,2}) for(int ch=1;ch<=4;ch++) for(int trial=0;trial<5;trial++){
    Amp a; a.prepare(48000,osf); AmpParams p; p.channel=ch;
    double* f[]={&p.gain,&p.bass,&p.mid,&p.treble,&p.volume,&p.master,&p.presence,&p.deep};
    for(int i=0;i<8;i++){ double r=(trial==0)?0.0:(trial==1)?1.0:rand()/double(RAND_MAX); *f[i]=r; }
    p.bright=(trial&1); a.setParams(p);
    for(int kind=0;kind<4;kind++){
      double amp_=(kind==0)?0.0:(kind==1)?0.05:(kind==2)?0.5:5.0; double pk=0; bool ok=true;
      for(int i=0;i<48000;i++){
        double x=amp_*((kind==3)?((i/60)%2?1:-1):std::sin(2*kPi*110*i/48000.0)*((i>24000)?1.0:0.2));
        if(i==30000) { p.gain=1-p.gain; p.volume=1-p.volume; a.setParams(p); }   // cambio manopole al volo
        double y=a.process(x); if(!std::isfinite(y)){ok=false;break;} pk=std::max(pk,std::fabs(y));
      }
      runs++; if(!ok||pk>20){bad++; printf("PROBLEMA osf%d ch%d trial%d kind%d picco %g ok=%d\n",osf,ch,trial,kind,pk,ok);} worst=std::max(worst,pk);
    }
  }
  printf("%d prove, %d problemi, picco massimo in uscita %.2f\n",runs,bad,worst);
}
