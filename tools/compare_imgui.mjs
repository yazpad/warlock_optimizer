// Exact native report replay between presentation modes; not a performance claim.
import path from 'node:path';
import {readJson,check,save,cli} from './tooling.mjs';
import {compare} from './compare_variant.mjs';
cli(()=>{
 const [referencePath,candidatePath,...rest]=process.argv.slice(2);
 check(referencePath&&candidatePath&&(!rest.length||(rest.length===2&&rest[0]==='--output')),'Usage: node tools/compare_imgui.mjs headless/summary.json gui/summary.json [--output file]');
 const reference=readJson(referencePath),candidate=readJson(candidatePath);
 check(reference.status==='complete'&&candidate.status==='complete','Only complete frontend evaluations can be replayed');
 check(reference.rawReports&&candidate.rawReports,'Replay requires --raw-reports on both runs; aggregate-only timing runs remain separate');
 check(reference.runs.length===candidate.runs.length,'Frontend repeat count mismatch');
 const gates=reference.runs.map((run,i)=>{
  const other=candidate.runs[i];
  const a=readJson(path.join(path.dirname(referencePath),run.nativeResult));
  const b=readJson(path.join(path.dirname(candidatePath),other.nativeResult));
  check(typeof a.runtime.engineBuildKey==='string'&&a.runtime.engineBuildKey.length===64&&typeof a.runtime.nativeRunnerHash==='string'&&a.runtime.nativeRunnerHash.length===64,'Missing native engine/runner identity');
  check(a.runtime.engineBuildKey===b.runtime.engineBuildKey&&a.runtime.nativeRunnerHash===b.runtime.nativeRunnerHash,'Native engine/runner identity mismatch');
  return compare(a,path.join(path.dirname(referencePath),`native-${i+1}.bin`),b,path.join(path.dirname(candidatePath),`native-${i+1}.bin`),{sameVariant:true});
 });
 const report={status:'passed',referencePresentation:reference.presentation,candidatePresentation:candidate.presentation,gates,note:'Every raw report word matched across presentation modes. This is correctness evidence; aggregate-only paired hardware timing is a separate gate.'};
 if(rest.length)save(rest[1],report);
 console.log(`PASS: ${gates.length} frontend replay(s); ${gates.reduce((sum,g)=>sum+g.comparedFights,0)} fights; all report words identical`);
});
