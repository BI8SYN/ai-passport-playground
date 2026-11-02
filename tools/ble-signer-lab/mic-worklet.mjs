class Capture extends AudioWorkletProcessor {
 constructor(){super();this.chunk=new Int16Array(80);this.used=0;}
 process(inputs){const channel=inputs[0]?.[0];if(channel)for(const sample of channel){this.chunk[this.used++]=Math.round(Math.max(-1,Math.min(1,sample))*32767);if(this.used===80){this.port.postMessage(this.chunk);this.chunk=new Int16Array(80);this.used=0;}}return true;}
}
registerProcessor('pcm-capture',Capture);
