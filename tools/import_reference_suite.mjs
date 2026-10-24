// Import saved evidence as diagnostics; this never certifies a parity suite.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const upstream=path.resolve(process.argv[2]||'/home/aaron-ma/yazpads_warlock_sim');
const reportPath=path.join(upstream,'FOREVER_SWEEP.json');
const bytes=fs.readFileSync(reportPath), report=JSON.parse(bytes);
const sha=data=>createHash('sha256').update(data).digest('hex');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'sim/classes/warlock/generated/manifest.json')));
for(const file of ['model.js','kernel.js']) {
 if(sha(fs.readFileSync(path.join(upstream,'threejs_webgl_des/src',file)))!==manifest.source.hashes[file]) throw Error('Regenerate combat snapshot first: '+file+' changed');
}
const model=await import(pathToFileURL(path.join(upstream,'threejs_webgl_des/src/model.js')));
const presets=['sm_ruin','aff_wrack','destro','demo_shadow_brand','demo_fire_brand'];
const scenarios=presets.map(preset=>{
 const matches=report.rows.filter(row=>row.preset===preset&&row.race==='HUMAN'&&row.profile==='baseline');
 if(matches.length!==1) throw Error('Expected one baseline scenario for '+preset);
 const row=matches[0];
 if(row.status!=='complete'||!row.config||!row.request) throw Error('Incomplete source scenario: '+row.id);
 const input={...row.config};const adaptations=[];
 if(!('distance' in model.DEFAULTS)&&'distance' in input){
  if(input.distance!==0)throw Error('Cannot adapt nonzero historical distance');
  delete input.distance;adaptations.push('Historical distance=0 is absent from current config schema; removed for diagnostic packing only.');
 }
 const config=model.validate(input);
 return {id:row.id,preset,scenarioId:report.baseline.scenarioId,duration:config.duration,
  packed:Array.from(model.packConfig(config)),resolvedConfig:row.config,referenceRequest:row.request,
  reference:row.forever,archivedWebgl:row.ours,adaptations,inputDifferences:row.inputDifferences??[],importDiagnostics:row.importDiagnostics??[]};
});
const files=['sim/classes/warlock/generated/fast.glsl','sim/classes/warlock/generated/detailed.glsl','sim/classes/warlock/generated/layout.lua'];
const kernelHashes=Object.fromEntries(files.map(file=>[file,sha(fs.readFileSync(path.join(root,file)))]));
const suite={schemaVersion:1,id:'forever-warlock-baseline-diagnostic-v1',classId:'warlock',acceptanceReady:false,
 acceptanceBlockers:['Archived source suite is diagnostic-only. Audit mechanics, APL and resolved inputs before acceptance.','Five baseline cases are a starting diagnostic subset, not the historical 300-case accuracy claim.'],
 thresholdDps:1,iterations:report.iterations,seed:report.seed,source:{report:'FOREVER_SWEEP.json',sha256:sha(bytes),foreverRevision:report.source.revision,foreverHashes:report.source.hashes,originalAcceptance:report.acceptance,
  archivedModelHash:report.localHashes['threejs_webgl_des/src/model.js'],archivedKernelHash:report.localHashes['threejs_webgl_des/src/kernel.js'],snapshotSource:manifest.source},kernelHashes,scenarios};
const destination=path.join(root,'data/validation');fs.mkdirSync(destination,{recursive:true});
const json=JSON.stringify(suite,null,2)+'\n';fs.writeFileSync(path.join(destination,'forever_baseline.json'),json);
function lua(value){
 if(Array.isArray(value)) return '{'+value.map(lua).join(',')+'}';
 if(value&&typeof value==='object') return '{'+Object.entries(value).map(([k,v])=>'['+JSON.stringify(k)+']='+lua(v)).join(',')+'}';
 if(value===null)return 'nil';return JSON.stringify(value);
}
const runtime={suiteId:suite.id,suiteHash:sha(json),kernelHashes,iterations:suite.iterations,seed:suite.seed,scenarios:scenarios.map(s=>({id:s.id,duration:s.duration,packed:s.packed}))};
fs.writeFileSync(path.join(destination,'forever_baseline.lua'),'-- Generated diagnostic suite, not approved parity evidence.\nreturn '+lua(runtime)+'\n');
console.log('Imported '+scenarios.length+' diagnostic reference scenarios; suite SHA-256 '+sha(json));
