export const SERVICE = '3fc10001-8b80-4f3a-9b22-4a4700000001';
export const RX = '3fc10002-8b80-4f3a-9b22-4a4700000001';
export const TX = '3fc10003-8b80-4f3a-9b22-4a4700000001';
export const CHANGED = '3fc10004-8b80-4f3a-9b22-4a4700000001';
// Public RFC8032 TEST 1 seed. NEVER use for a real account/certificate.
export const TEST_SEED = '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60';
export const TEST_PUBLIC = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a';
export function hex(bytes) {return Array.from(bytes, b=>b.toString(16).padStart(2,'0')).join('');}
export function unhex(text, bytes) {
  if(typeof text!=='string'||text.length!==bytes*2||!/^[0-9a-f]+$/i.test(text))throw Error(`需要 ${bytes} 字节十六进制`);
  return Uint8Array.from(text.match(/../g),s=>parseInt(s,16));
}
export async function identity(seed, subtle=crypto.subtle) {
  const raw=unhex(seed.trim(),32),prefix=unhex('302e020100300506032b657004220420',16);
  const pkcs8=new Uint8Array(48);pkcs8.set(prefix);pkcs8.set(raw,16);
  const privateKey=await subtle.importKey('pkcs8',pkcs8,{name:'Ed25519'},true,['sign']);
  const jwk=await subtle.exportKey('jwk',privateKey);
  const b64=jwk.x.replace(/-/g,'+').replace(/_/g,'/');
  const publicBytes=Uint8Array.from(atob(b64),c=>c.charCodeAt(0));
  const publicKey=await subtle.importKey('raw',publicBytes,{name:'Ed25519'},true,['verify']);
  return {privateKey,publicKey,key:hex(publicBytes)};
}
export function frames(object) {
  const raw=new TextEncoder().encode(JSON.stringify(object)+'\n');
  if(raw.length>511)throw Error('请求超过协议长度限制');
  const result=[];for(let i=0;i<raw.length;i+=20)result.push(raw.slice(i,i+20));return result;
}
export async function signature(id,message,subtle=crypto.subtle) {
  return new Uint8Array(await subtle.sign('Ed25519',id.privateKey,new TextEncoder().encode(message)));
}
export async function selftest(subtle=crypto.subtle) {
  const id=await identity(TEST_SEED,subtle);if(id.key!==TEST_PUBLIC)throw Error('公钥测试失败');
  const result=await signature(id,'',subtle);
  if(hex(result)!=='e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b')throw Error('RFC8032 签名测试失败');
  if(!await subtle.verify('Ed25519',id.publicKey,result,new Uint8Array()))throw Error('验签测试失败');
  result[0]^=1;if(await subtle.verify('Ed25519',id.publicKey,result,new Uint8Array()))throw Error('错误签名未被拒绝');
  return 'RFC8032 Ed25519：公钥、签名、验签及篡改拒绝 PASS';
}
