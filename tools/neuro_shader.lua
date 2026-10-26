-- Opt-in full-fight neural inference. Each lane runs a policy without host round trips.
local policy=require('tools.policy_shader')
local neuro=require('tools.neuroevolution')
local M={traceWords=8+#neuro.features,stride=8,capacity=policy.capacity}
M.words=32+policy.capacity*M.traceWords
function M.instrument(source)
  local s=policy.instrument(source)
  s=s:gsub('regretGlobal%*'..policy.stride..'u','regretGlobal*8u')
  s=s:gsub('lane%*32u','lane*control(6u)')
  -- This adapter keeps baseline reports identical but does not need rollout capture.
  local start=assert(s:find('void decide(){',1,true))
  local finish=assert(s:find('\nvoid creditManaRegen',start,true))
 local prelude=assert(s:sub(start,finish-1):match('void decide%(%){.-\n(.-) policyPrepare%(%);'))
  local features,capture={},{}
  for i,f in ipairs(neuro.features) do
    features[#features+1]=(' x[%d]=%s;'):format(54+i,f[2])
    capture[#capture+1]=(' resultData[base+%du]=floatBitsToUint(x[%d]);'):format(7+i,54+i)
  end
  s=s:sub(1,start-1)..[[
float remaining(uint until,float scale){return min(1.0,float(until>s.now?until-s.now:0u)/scale);}
float x[INPUTS];
void neuralFeatures(uint mask,uint apl){
 x[0]=min(1.0,float(s.now)/float(c.end));
 x[1]=min(2.0,s.mana/10000.0);x[2]=min(2.0,s.total/1000000.0);
 for(uint a=0u;a<26u;a++){x[3u+a]=(mask&(1u<<a))!=0u?1.0:0.0;x[29u+a]=a==apl?1.0:0.0;}
]]..table.concat(features,'\n')..[[
}

float neuralWeight(uint i){return uintBitsToFloat(configData[control(4u)+i]);}
uint neuralApl(){
 if(policyResumeRow<numAplRules)return policyCanonical(getConfigWord(currentCfgIdx,APL_HEADER0_OFFSET+policyResumeRow)&255u);
 return s.mana>=cost(c.filler)?(c.filler==0u?16u:c.filler==4u?12u:13u):1u;
}
uint neuralSelect(uint mask,uint apl){
 neuralFeatures(mask,apl);
 float h[12];
 for(uint j=0u;j<12u;j++){
  uint k=j*INPUT_STRIDEu;
  float v=neuralWeight(k++);
  for(uint i=0u;i<INPUTSu;i++)v+=neuralWeight(k++)*x[i];
  h[j]=tanh(v);
 }
 float best=-3.402823e38;uint chosen=0u;
 for(uint a=0u;a<26u;a++){
  if((mask&(1u<<a))==0u)continue;
  uint k=HIDDEN_WORDSu+a*13u;
  float v=neuralWeight(k++);
  for(uint j=0u;j<12u;j++)v+=neuralWeight(k++)*h[j];
  v+=a==apl?APL_PRIOR:0.0;
  if(v>best){best=v;chosen=a;}
 }
 return chosen;
}
void decide(){
 // Retain the original pre-decision off-GCD effects.
]]..prelude..[[
 policyPrepare();
 uint ordinal=regretDecisions++;
 regretTaken=0u;regretForced=0u;
 uint mask=0u,apl=0u,chosen=0u;
 bool neural=control(0u)==0xfffffffeu;
 bool capture=neural&&control(2u)!=0u;
 if(neural){
  mask=regretMask();apl=neuralApl();chosen=neuralSelect(mask,apl);
  if(ordinal>=1024u)regretInvalid++;
  regretForced=chosen;
 }
 uint at=s.now;float mana=s.mana,damage=s.total;
 uvec2 signature=uvec2(0u);
 if(capture)signature=policyHash();
 policyDecide();
 if(neural&&chosen!=0u&&policyCanonical(regretTaken)!=chosen)regretInvalid++;
 regretForced=0u;
 if(capture&&ordinal<1024u){
  uint base=regretLane*control(6u)+32u+ordinal*TRACE_WORDSu;
  resultData[base]=at;resultData[base+1u]=floatBitsToUint(mana);
  resultData[base+2u]=floatBitsToUint(damage);resultData[base+3u]=chosen;
  resultData[base+4u]=apl;resultData[base+5u]=signature.x;resultData[base+6u]=signature.y;
  resultData[base+7u]=mask;
]]..table.concat(capture,'\n')..[[
 }
}
]]..s:sub(finish)
  s=s:gsub('INPUT_STRIDE',tostring(neuro.inputs+1)):gsub('INPUTS',tostring(neuro.inputs))
    :gsub('HIDDEN_WORDS',tostring((neuro.inputs+1)*neuro.hidden)):gsub('TRACE_WORDS',tostring(M.traceWords))
    :gsub('APL_PRIOR',tostring(neuro.aplPrior))
  return s:gsub('NEURAL_WORDS',tostring(M.words)..'u')
end
return M
