import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
export const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
export const readJson=p=>JSON.parse(fs.readFileSync(p,'utf8'));
export const sha=data=>createHash('sha256').update(data).digest('hex');
export const equal=(a,b)=>JSON.stringify(canonical(a))===JSON.stringify(canonical(b));
function canonical(v){return Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;}
export function check(ok,message){if(!ok)throw Error(message);}
export function run(file,args=[],options={}){
 if(options.stdio)return execFileSync(file,args,{cwd:root,timeout:1800000,...options});
 // File-backed capture also works in restricted environments without pipe polling.
 return temporary(dir=>{
  const out=fs.openSync(path.join(dir,'stdout'),'w'),err=fs.openSync(path.join(dir,'stderr'),'w');
  try{execFileSync(file,args,{cwd:root,timeout:1800000,...options,stdio:['ignore',out,err]});}
  catch(e){e.message+='\n'+fs.readFileSync(path.join(dir,'stderr'),'utf8');throw e;}
  finally{fs.closeSync(out);fs.closeSync(err);}
  return fs.readFileSync(path.join(dir,'stdout'),'utf8');
 });
}
export function temporary(fn){const dir=fs.mkdtempSync(path.join(os.tmpdir(),'fastsim-'));try{return fn(dir);}finally{fs.rmSync(dir,{recursive:true,force:true});}}
export function compileShader(source,destination,target){
 check(source.includes('#pragma language glsl4'),'Shader language marker changed');
 let binding=0;
 source=source.replace('#pragma language glsl4','#version 450\n#define love_GlobalThreadID gl_GlobalInvocationID').replaceAll('layout(std430)',()=>`layout(std430, set=0, binding=${binding++})`);
 fs.writeFileSync(destination+'.comp',source+'\nvoid main() { computemain(); }\n');
 run('glslangValidator',['-V',...(target?['--target-env',target]:[]),destination+'.comp','-o',destination]);run('spirv-val',[destination]);
}
export function layout(){return JSON.parse(run('lua',['-e','print(require("app.json").encode(require("sim.classes.warlock.generated.layout")))']));}
export function save(p,value){fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n');}
export function cli(fn){try{fn();}catch(e){console.error(e.message);process.exitCode=2;}}
