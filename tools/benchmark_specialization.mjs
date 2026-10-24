import {pathToFileURL} from 'node:url';
import fs from 'node:fs';
import path from 'node:path';
import {run,readJson,check,save,temporary,cli} from './tooling.mjs';
import {compare} from './compare_variant.mjs';
export function experiment(args=process.argv.slice(2),execute=run){
 const opts={'--total-fights':'1000000','--mode':'fast','--batch-size':'65536','--runs':'2','--warmup-batches':'2','--output-dir':'artifacts/specialization'};
 while(args.length){const k=args.shift(),v=args.shift();check(v&&(k in opts||k==='--subgroup-size'),'Invalid benchmark argument: '+k);opts[k]=v;}
 const runs=Number(opts['--runs']);check(Number.isInteger(runs)&&runs>=1&&runs<=10,'runs must be 1–10');
 const variants=['generic','shared-features'];
 if(opts['--subgroup-size']){check([4,8,16,32,64].includes(Number(opts['--subgroup-size'])),'Invalid subgroup size');variants.push('shared-features-wave'+opts['--subgroup-size']);}
 const dir=opts['--output-dir'];fs.mkdirSync(dir,{recursive:true});
 const summary={status:'running',targetParityMaeDps:1,parityNote:'Exact replay preserves generic outputs; accepted Forever parity remains separate.',measurements:[],replayGates:[]};
 try{
  temporary(temp=>{
   let baseline;const baselineRaw=path.join(temp,'reference.bin');
   for(let repeat=0;repeat<runs;repeat++)for(const variant of repeat%2?[...variants].reverse():variants){
    const n=summary.measurements.length+1,output=path.join(dir,String(n).padStart(2,'0')+'-'+variant+'.json'),raw=path.join(temp,n+'.bin');
    const cmd=['tools/run_native.lua','--generic'];
    if(variant!=='generic')cmd.push('--specialize-features');
    if(variant.startsWith('shared-features-wave'))cmd.push('--subgroup-size',opts['--subgroup-size']);
    for(const k of ['--total-fights','--mode','--batch-size','--warmup-batches'])cmd.push(k,opts[k]);
    cmd.push('--require-gpu','--pipeline-stats','--output',output,'--raw-output',raw);
    console.log(`Experiment ${n}/${runs*variants.length}: ${variant}`);execute('luajit',cmd,{stdio:'inherit'});
    const result=readJson(output);
    if(!baseline){baseline=result;fs.renameSync(raw,baselineRaw);}else{summary.replayGates.push(compare(baseline,baselineRaw,result,raw));fs.unlinkSync(raw);}
    summary.measurements.push({variant,file:output,timing:result.timing,optimization:result.optimization,runtime:result.runtime,device:result.probe.device});
   }
  });
  summary.medians={};
  for(const variant of variants){
   summary.medians[variant]={};
   for(const metric of ['gpuComputeMs','executeReadbackMs','wallMs']){
    const values=summary.measurements.filter(m=>m.variant===variant).map(m=>m.timing[metric]).filter(v=>typeof v==='number'&&Number.isFinite(v)).sort((a,b)=>a-b);
    if(values.length){const n=values.length;summary.medians[variant][metric]={median:(values[Math.floor((n-1)/2)]+values[Math.floor(n/2)])/2,min:values[0],max:values.at(-1)};}
   }
  }
  summary.status='passed-replay';console.log('PASS: exact report replay. Timings include diagnostic raw-output overhead.');
 }catch(e){summary.status='failed';summary.error=e.message;throw e;}
 finally{save(path.join(dir,'summary.json'),summary);}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href)cli(()=>experiment());
