'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
let Processor;const emitted=[];
const context={sampleRate:48000,Float32Array,Int16Array,Math,
 AudioWorkletProcessor:class{constructor(){this.port={postMessage:m=>emitted.push(m),onmessage:null};}},
 registerProcessor:(name,cls)=>{Processor=cls;}};
vm.runInNewContext(fs.readFileSync(__dirname+'/audio-worklet.js','utf8'),context);
const node=new Processor(),output=[new Float32Array(128)];
const message=m=>node.port.onmessage({data:m});
message({type:'config',enabled:true,capture:false});
message({type:'audio',buffer:new Int16Array(800).fill(8000).buffer});
let audible=false;for(let i=0;i<80;i++){node.process([], [output]);if(output[0].some(x=>x>.1))audible=true;}
assert(audible,'a100ms clip must drain without waiting for later speech');
message({type:'flush'});for(let i=0;i<15;i++){node.process([], [output]);assert(output[0].every(x=>x===0));}
message({type:'audio',buffer:new Int16Array(400).fill(-8000).buffer});
message({type:'config',enabled:false,capture:false});message({type:'flush'});message({type:'config',enabled:true,capture:true});
for(let i=0;i<30;i++)node.process([[new Float32Array(128).fill(.25)]],[output]);
assert(emitted.some(m=>m.type==='mic'&&m.buffer.byteLength===160));
assert(output[0].every(x=>x===0),'listen/stream flush must not replay previous speech');
console.log('AudioWorklet short-tail / flush / microphone8k frames PASS');
