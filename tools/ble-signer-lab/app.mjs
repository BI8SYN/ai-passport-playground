import {SERVICE,RX,TX,CHANGED,TEST_SEED,identity,hex,signature,frames,selftest} from './protocol.mjs';
const $=id=>document.getElementById(id);
let signer,device,rx,tx,timer,status,lastReply,lastHandled='',running=false,batching=false;
let serial=Promise.resolve();const records=[];
function log(event,data={}){const row={at:new Date().toISOString(),event,...data};records.push(row);$('log').textContent=records.slice(-65).map(r=>JSON.stringify(r)).join('\n');$('log').scrollTop=$('log').scrollHeight;}
function queue(fn){const next=serial.then(fn);serial=next.catch(()=>{});return next;}
function fail(error){log('error',{message:error.message});}
function controls(){const live=!!device?.gatt?.connected&&!!rx&&!!tx;for(const id of ['disconnect','bind','request','batch'])$(id).disabled=!live||!signer;$('replay').disabled=!live||!lastReply;$('connect').disabled=live;}
async function load(){const next=await identity($('seed').value);signer=next;$('public').textContent=signer.key;lastHandled='';log('identity_loaded',{public:signer.key});controls();}
async function send(object){await queue(async()=>{if(!rx||!device?.gatt?.connected)throw Error('BLE 未连接');for(const frame of frames(object))await rx.writeValueWithResponse(frame);});}
function disconnected(){clearInterval(timer);timer=null;rx=tx=null;lastHandled='';batching=false;$('state').textContent='已断开；设备保留绑定';controls();log('disconnected');}
async function poll(){
  if(running||!tx||!device?.gatt?.connected)return;running=true;
  try{
    const value=await queue(()=>tx.readValue());const next=JSON.parse(new TextDecoder().decode(value));
    if(next.v!==1)throw Error('不兼容的固件协议');
    if(!status||next.passed!==status.passed||next.rejected!==status.rejected||next.timeouts!==status.timeouts||next.bound!==status.bound||next.key!==status.key)log('device_status',{passed:next.passed,rejected:next.rejected,timeouts:next.timeouts,rtt_ms:next.rtt_ms,bound:next.bound,key:next.key,note:next.note});
    status=next;$('passed').textContent=status.passed;$('rejected').textContent=status.rejected;$('rtt').textContent=status.rtt_ms;
    $('state').textContent=status.note;$('binding').textContent=status.bound?`设备已绑定公钥：${status.key}${status.key===signer?.key?'（当前身份）':'（其他身份）'}`:status.candidate?`待设备确认：${status.candidate}`:'设备尚未绑定';
    $('pending').textContent=status.pending?JSON.stringify({id:status.id,message:status.message},null,2):`空闲 · 超时 ${status.timeouts} 次`;
    if(status.pending&&$('auto').checked&&signer&&status.key===signer.key){
      const marker=`${status.id}:${status.message}`;
      if(marker!==lastHandled){
        lastHandled=marker;const start=performance.now();const sig=await signature(signer,status.message);
        const corrupt=$('bad').checked;if(corrupt){sig[0]^=1;$('bad').checked=false;}
        const reply={op:'signature',id:status.id,sig:hex(sig)};
        await send(reply);lastReply=reply;controls();
        log('signature_sent',{id:status.id,corrupt,local_sign_and_write_ms:Math.round(performance.now()-start)});
      }
    }
  }catch(error){fail(error);}finally{running=false;}
}
$('seed').value=TEST_SEED;
$('apply').onclick=()=>load().catch(fail);
$('random').onclick=()=>{if(device?.gatt?.connected){log('identity_switch',{note:'设备绑定不变；用于验证拒绝其他身份'});}$('seed').value=hex(crypto.getRandomValues(new Uint8Array(32)));load().catch(fail);};
$('connect').onclick=async()=>{try{
  if(!navigator.bluetooth)throw Error('请使用支持 Web Bluetooth 的电脑 Chrome，并通过 localhost 打开页面');
  device=await navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]}]});
  device.addEventListener('gattserverdisconnected',disconnected);
  const server=await device.gatt.connect();const service=await server.getPrimaryService(SERVICE);
  rx=await service.getCharacteristic(RX);tx=await service.getCharacteristic(TX);
  // Separate notification and read characteristics: a 1-byte notification must
  // never replace a readValue() result in the browser characteristic cache.
  const changed=await service.getCharacteristic(CHANGED);
  changed.addEventListener('characteristicvaluechanged',()=>poll());await changed.startNotifications();
  lastHandled='';timer=setInterval(poll,500);controls();log('connected',{name:device.name});await poll();
}catch(error){device?.gatt?.disconnect();disconnected();fail(error);}};
$('disconnect').onclick=()=>device?.gatt?.disconnect();
$('bind').onclick=()=>send({op:'bind',key:signer.key}).then(()=>log('bind_requested',{public:signer.key})).catch(fail);
$('request').onclick=()=>send({op:'request'}).catch(fail);
$('replay').onclick=()=>send(lastReply).then(()=>log('replay_sent',{id:lastReply.id})).catch(fail);
$('batch').onclick=async()=>{
  if(batching)return;batching=true;
  try{
    if(!status?.bound||status.key!==signer.key||!$('auto').checked||$('bad').checked||status.pending)throw Error('批量测试需要当前身份已绑定、空闲且自动回复开启');
    for(let i=0;i<10&&batching;i++){
      const before=status.passed;await send({op:'request'});const deadline=performance.now()+12000;
      while(batching&&performance.now()<deadline&&status.passed===before)await new Promise(r=>setTimeout(r,100));
      if(!batching)throw Error('批量测试因断连取消');
      if(status.passed!==before+1)throw Error(`第 ${i+1} 次未通过`);
      log('batch_pass',{iteration:i+1,rtt_ms:status.rtt_ms});
    }
    log('batch_complete');
  }catch(error){fail(error);}finally{batching=false;}
};
$('auto').onchange=()=>{if($('auto').checked)poll();};
$('download').onclick=()=>{const blob=new Blob([JSON.stringify({kind:'ble-signer-lab',formal_service:false,records},null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='ble-signer-lab-results.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);};
window.addEventListener('beforeunload',()=>device?.gatt?.disconnect());
try{$('crypto').textContent=await selftest();await load();log('crypto_selftest_pass');}catch(error){$('crypto').textContent=error.message;fail(error);}
