import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
import {readJson,equal,check,save,cli} from './tooling.mjs';
export function compare(reference,referenceRaw,candidate,candidateRaw,{sameVariant=false}={}){
 for(const key of ['schemaVersion','classId','suiteHash','kernelHashes','inputSnapshotHash','mode','iterations','seed']){
  check(reference[key]!==undefined&&equal(reference[key],candidate[key]),'Replay identity mismatch: '+key);
 }
 check(reference.status==='complete'&&candidate.status==='complete','Incomplete evaluation');
 if(sameVariant)check(equal(reference.optimization,candidate.optimization),'Frontend shader optimization mismatch');
 else check(reference.optimization.variant==='generic','Reference must be generic');
 const device=r=>r.probe?.device??r.runtime.device;
 check(device(reference)&&device(reference)===device(candidate),'Replay device mismatch');
 for(const r of [reference,candidate]){
  const ids=r.results.map(x=>x.candidateId);
  check(ids.length>0&&ids.every(x=>typeof x==='string'&&x.trim())&&new Set(ids).size===ids.length,'Invalid candidate IDs');
  check(r.results.every(x=>x.status==='complete'&&x.sampleCount===r.iterations),'Incomplete candidate samples');
 }
 check(equal(reference.results.map(x=>x.candidateId),candidate.results.map(x=>x.candidateId)),'Candidate ordering mismatch');
 const manifest=readJson('sim/classes/warlock/generated/manifest.json');
 check(['fast','detailed'].includes(reference.mode),'Invalid report mode');
 const words=reference.mode==='fast'?manifest.fastWords:manifest.detailedWords;
 const fights=reference.results.length*reference.iterations,bytes=fights*words*4;
 check(fs.statSync(referenceRaw).size===bytes&&fs.statSync(candidateRaw).size===bytes,'Raw report size mismatch');
 const a=fs.openSync(referenceRaw,'r'),b=fs.openSync(candidateRaw,'r');
 try{
  const size=Math.max(1,Math.floor(1048576/(words*4)))*words*4;
  const first=Buffer.alloc(size),second=Buffer.alloc(size);
  for(let offset=0;offset<bytes;offset+=size){
   const length=Math.min(size,bytes-offset);
   check(fs.readSync(a,first,0,length,offset)===length&&fs.readSync(b,second,0,length,offset)===length,'Short report read');
   check(first.subarray(0,length).equals(second.subarray(0,length)),'Report mismatch at chunk offset '+offset);
   for(let j=0;j<length;j+=words*4){
    const total=first.readFloatLE(j),status=first.readUInt32LE(j+4);
    check((status&65535)===1&&(status>>>16)<=manifest.heapCapacity&&Number.isFinite(total)&&total>=0,'Invalid baseline report at byte '+(offset+j));
   }
  }
 }finally{fs.closeSync(a);fs.closeSync(b);}
 return {status:'passed',comparedFights:fights,comparedWords:fights*words,gate:sameVariant?'Every report word is bit-identical across frontend presentation modes; accepted Forever parity remains separate.':'Every report word is bit-identical to generic; accepted Forever parity remains separate.'};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href)cli(()=>{
 const [a,ar,b,br,...rest]=process.argv.slice(2);check(a&&ar&&b&&br,'Usage: node tools/compare_variant.mjs reference.json reference.bin candidate.json candidate.bin [--output file]');
 check(!rest.length||(rest.length===2&&rest[0]==='--output'),'Invalid arguments');
 const report=compare(readJson(a),ar,readJson(b),br);if(rest.length)save(rest[1],report);console.log('PASS: '+report.comparedFights+' fights, all report words identical');
});
