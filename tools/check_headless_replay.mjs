import fs from 'node:fs';
import {readJson,equal,check,layout,save,cli} from './tooling.mjs';
cli(()=>{
 const [a,ar,b,br,...rest]=process.argv.slice(2);check(a&&ar&&b&&br,'Usage: node tools/check_headless_replay.mjs fast.json fast.bin detailed.json detailed.bin [--output file]');
 check(!rest.length||(rest.length===2&&rest[0]==='--output'),'Invalid arguments');
 const fast=readJson(a),detailed=readJson(b),l=layout();
 check(fast.status==='complete'&&detailed.status==='complete'&&fast.mode==='fast'&&detailed.mode==='detailed','Invalid replay modes/status');
 for(const k of ['suiteHash','inputSnapshotHash','seed','iterations','kernelHashes'])check(fast[k]!==undefined&&equal(fast[k],detailed[k]),'Replay identity mismatch: '+k);
 const ids=fast.results.map(r=>r.candidateId);check(ids.length&&new Set(ids).size===ids.length&&equal(ids,detailed.results.map(r=>r.candidateId)),'Candidate mapping mismatch');
 const count=ids.length*fast.iterations,raws=[fs.readFileSync(ar),fs.readFileSync(br)];
 const common=Object.keys(l.fields.fast).filter(k=>k in l.fields.detailed).sort();
 function decode(raw,mode,lane){
  const stride=mode==='fast'?l.fastWords:l.detailedWords;
  check(raw.length===count*stride*4,'Raw report size mismatch');
  const s={};
  for(const [name,f] of Object.entries(l.fields[mode])){
   const offset=(lane*stride+f.word)*4;let v=f.type==='f32'?raw.readFloatLE(offset):raw.readUInt32LE(offset);
   if(f.part==='low')v%=65536;else if(f.part==='high')v=Math.floor(v/65536);
   if(f.type==='fraction')v/=65535;
   check(Number.isFinite(v),'Nonfinite report field');s[name]=v;
  }
  check(s.done===1&&s.total>=0&&s.highWater<=l.heapCapacity,'Invalid fight');return s;
 }
 let differenceCount=0;const differences=[];
 for(let lane=0;lane<count;lane++){
  const first=decode(raws[0],'fast',lane),second=decode(raws[1],'detailed',lane);
  for(const field of common)if(first[field]!==second[field]){differenceCount++;if(differences.length<30)differences.push({candidateId:ids[Math.floor(lane/fast.iterations)],fight:lane%fast.iterations,field,fast:first[field],detailed:second[field]});}
 }
 const report={scope:'Actual Vulkan fast/detailed common-field replay; not Forever parity or LOVE integration',suiteHash:fast.suiteHash,kernelHashes:fast.kernelHashes,sampleCount:count,fields:common,differenceCount,differences};
 if(rest.length)save(rest[1],report);check(!differenceCount,JSON.stringify(report));console.log(`PASS: ${count} fights, ${common.length} common fields match exactly`);
});
