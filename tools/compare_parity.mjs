import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
import {equal,check,sha,readJson,save,cli} from './tooling.mjs';
function number(v,label,min=0){check(typeof v==='number'&&Number.isFinite(v)&&v>=min,'Invalid '+label);return v;}
// Acklam rational approximation of the standard normal inverse CDF.
function normalQuantile(p){
 const a=[-39.6968302866538,220.946098424521,-275.928510446969,138.357751867269,-30.6647980661472,2.50662827745924];
 const b=[-54.4760987982241,161.585836858041,-155.698979859887,66.8013118877197,-13.2806815528857];
 const c=[-0.00778489400243029,-0.322396458041136,-2.40075827716184,-2.54973253934373,4.37466414146497,2.93816398269878];
 const d=[0.00778469570904146,0.32246712907004,2.445134137143,3.75440866190742];
 if(p>0.97575)return -normalQuantile(1-p);
 if(p<0.02425){const q=Math.sqrt(-2*Math.log(p));return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);}
 const q=p-0.5,r=q*q;return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q/(((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1);
}
export function compare(suite,result,suiteHash){
 check(suite.schemaVersion===1&&result.schemaVersion===1,'Unsupported schema');
 check(suite.classId==='warlock'&&result.classId==='warlock','Class mismatch');
 check(result.suiteHash===suiteHash&&result.suiteId===suite.id,'Suite identity/hash mismatch');
 check(suite.kernelHashes&&Object.keys(suite.kernelHashes).length&&equal(result.kernelHashes,suite.kernelHashes),'Kernel/layout snapshot mismatch');
 check(result.status==='complete','Evaluation incomplete');
 check(result.runtime?.backend?.toLowerCase()==='vulkan','Actual Vulkan backend required');
 check(result.iterations===suite.iterations&&result.seed===suite.seed,'Iteration/seed mismatch');
 const expected=suite.scenarios,actual=result.results;
 check(expected?.length&&actual?.length===expected.length,'Scenario count mismatch');
 const ids=expected.map(r=>r.id),actualIds=actual.map(r=>r.candidateId);
 check([...ids,...actualIds].every(x=>typeof x==='string'&&x.trim()),'Invalid scenario IDs');
 check(new Set(ids).size===ids.length&&new Set(actualIds).size===ids.length&&ids.every(x=>actualIds.includes(x)),'Duplicate/missing IDs');
 const byId=new Map(actual.map(r=>[r.candidateId,r]));const z=normalQuantile(1-0.05/(2*expected.length));
 const rows=expected.map(s=>{
  const ref=s.reference,m=byId.get(s.id);
  check(m.status==='complete'&&m.sampleCount===suite.iterations,'Incomplete samples: '+s.id);
  const nr=number(ref.n,'reference count',2),nm=number(m.sampleCount,'sample count',2);check(Number.isInteger(nr)&&Number.isInteger(nm),'Noninteger count');
  const delta=number(m.meanDps,'measured mean')-number(ref.mean,'reference mean');
  const radius=z*Math.sqrt(number(ref.sd,'reference sd')**2/nr+number(m.sd,'measured sd')**2/nm);
  return {id:s.id,referenceMean:ref.mean,measuredMean:m.meanDps,deltaDps:delta,absoluteErrorDps:Math.abs(delta),deltaCi95SimultaneousHalfWidth:radius,absoluteErrorLower:Math.max(0,Math.abs(delta)-radius),absoluteErrorUpper:Math.abs(delta)+radius,referenceSamples:nr,measuredSamples:nm};
 });
 const average=k=>rows.reduce((a,r)=>a+r[k],0)/rows.length;
 const mae=average('absoluteErrorDps'),lower=average('absoluteErrorLower'),upper=average('absoluteErrorUpper'),threshold=number(suite.thresholdDps,'threshold');
 const blockers=[...(suite.acceptanceBlockers||[])];
 for(const s of expected)if(['inputDifferences','importDiagnostics','adaptations'].some(k=>s[k]?.length))blockers.push(s.id+': unresolved source input/APL/schema audit');
 const decision=suite.acceptanceReady!==true||blockers.length?'diagnostic-only':upper<=threshold?'pass':lower>threshold?'fail':'inconclusive';
 return {schemaVersion:1,suiteId:suite.id,suiteHash,decision,scenarioCount:rows.length,maeDps:mae,maeApprox95Lower:lower,maeApprox95Upper:upper,thresholdDps:threshold,observedWithinTarget:mae<=threshold,acceptanceBlockers:blockers,intervalMethod:'Approximate normal independent-stream intervals, Bonferroni across scenarios; bounds averaged for MAE. Sampling uncertainty only, not mechanics accuracy.',rows};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href)cli(()=>{
 const args=process.argv.slice(2),file=args.shift();check(file,'Usage: node tools/compare_parity.mjs results.json [--suite file] [--output file]');
 let suite='data/validation/forever_baseline.json',output;
 while(args.length){const k=args.shift(),v=args.shift();check(v&&['--suite','--output'].includes(k),'Invalid arguments');if(k==='--suite')suite=v;else output=v;}
 const bytes=fs.readFileSync(suite),report=compare(JSON.parse(bytes),readJson(file),sha(bytes));
 if(output)save(output,report);console.log(`${report.decision}: MAE ${report.maeDps.toFixed(4)} DPS; approximate 95% bounds [${report.maeApprox95Lower.toFixed(4)}, ${report.maeApprox95Upper.toFixed(4)}]`);
 process.exitCode=report.decision==='pass'?0:1;
});
