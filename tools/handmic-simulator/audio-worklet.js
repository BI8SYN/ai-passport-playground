class PassportAudio extends AudioWorkletProcessor {
  constructor() {
    super(); this.ring=new Float32Array(32768);this.read=0;this.write=0;this.count=0;this.phase=0;
    this.idleSamples=0;this.waiting=true;this.prebuffer=1600;this.enabled=false;this.capture=false;this.mic=[];this.sum=0;this.n=0;this.capturePhase=0;
    this.underruns=0;this.dropped=0;this.ticks=0;
    this.port.onmessage=({data:m})=>{
      if(m.type==='audio'){this.idleSamples=0;
        const samples=new Int16Array(m.buffer);
        for(const x of samples){if(this.count===this.ring.length){this.read=(this.read+1)%this.ring.length;this.count--;this.dropped++;}this.ring[this.write]=x/32768;this.write=(this.write+1)%this.ring.length;this.count++;}
        if(this.count>3200){let trim=this.count-1600;this.read=(this.read+trim)%this.ring.length;this.count-=trim;this.dropped+=trim;}
      } else if(m.type==='flush'){this.count=0;this.read=this.write=0;this.phase=0;this.waiting=true;this.idleSamples=0;}
      else if(m.type==='config'){this.enabled=!!m.enabled;this.capture=!!m.capture;}
    };
  }
  process(inputs,outputs){
    const output=outputs[0],len=output[0]?.length||128;
    this.idleSamples+=len;
    if(this.waiting&&(this.count>=this.prebuffer||(this.count>1&&this.idleSamples>=sampleRate*.05)))this.waiting=false;
    for(let i=0;i<len;i++){
      let value=0;
      if(this.enabled&&!this.waiting){
        if(this.count>=2){const a=this.ring[this.read],b=this.ring[(this.read+1)%this.ring.length];value=a+(b-a)*this.phase;this.phase+=8000/sampleRate;
          while(this.phase>=1&&this.count){this.phase--;this.read=(this.read+1)%this.ring.length;this.count--;}
        }else{this.waiting=true;this.underruns++;}
      }
      for(const channel of output)channel[i]=value;
    }
    if(this.capture&&inputs[0]?.[0])for(const sample of inputs[0][0]){
      this.sum+=sample;this.n++;this.capturePhase+=8000;
      if(this.capturePhase>=sampleRate){this.capturePhase-=sampleRate;this.mic.push(Math.max(-32768,Math.min(32767,Math.round(this.sum/this.n*32767))));this.sum=0;this.n=0;
        if(this.mic.length===80){const pcm=new Int16Array(this.mic);this.mic=[];this.port.postMessage({type:'mic',buffer:pcm.buffer},[pcm.buffer]);}
      }
    }
    if(++this.ticks%(Math.max(1,Math.round(sampleRate/128)))===0)this.port.postMessage({type:'stats',buffer_ms:this.count/8,underruns:this.underruns,dropped_samples:this.dropped});
    return true;
  }
}
registerProcessor('passport-audio',PassportAudio);
