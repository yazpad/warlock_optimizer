-- Internal packed boundary. Logical build resolution remains a separate layer.
local layout=require('sim.classes.warlock.generated.layout')
local M={}
local function integer(value,lo,hi,label)
 assert(type(value)=='number' and value==math.floor(value) and value>=lo and value<=hi,label..' out of range')
 return value
end
function M.validate(request)
 assert(type(request)=='table' and request.schemaVersion==1,'Expected packed evaluation schemaVersion 1')
 assert(request.classId=='warlock','Unsupported class')
 assert(request.mode=='fast' or request.mode=='detailed','Unsupported report mode')
 local iterations=integer(request.iterations,1,1048576,'iterations')
 local seed=integer(request.seed,0,4294967295,'seed')
 local batchSize=integer(request.batchSize or 4096,1,65536,'batchSize')
 local eventBudget=request.eventBudget
 if eventBudget~=nil then integer(eventBudget,1,1000000,'eventBudget') end
 assert(type(request.candidates)=='table' and #request.candidates>0 and #request.candidates<=1024,'Expected 1–1024 candidates')
 assert(#request.candidates*iterations<=16777216,'Evaluation exceeds total-fight limit')
 assert(request.specializeFeatures==nil or type(request.specializeFeatures)=='boolean','specializeFeatures must be boolean')
 assert(request.blockingReadback==nil or type(request.blockingReadback)=='boolean','blockingReadback must be boolean')
 assert(request.retainRawReports==nil or type(request.retainRawReports)=='boolean','retainRawReports must be boolean')
 local subgroupSize=request.subgroupSize
 if subgroupSize~=nil then
  integer(subgroupSize,4,64,'subgroupSize')
  assert(subgroupSize==4 or subgroupSize==8 or subgroupSize==16 or subgroupSize==32 or subgroupSize==64,'Unsupported subgroupSize')
 end
 assert(request.retainSamples==nil or type(request.retainSamples)=='boolean','retainSamples must be boolean')
 assert(not request.retainSamples or #request.candidates*iterations<=65536,'Diagnostic sample retention exceeds 65536 fights')
 assert(not request.retainRawReports or #request.candidates*iterations<=65536,'Raw report retention exceeds 65536 fights')
 local snapshot={schemaVersion=1,classId='warlock',mode=request.mode,iterations=iterations,seed=seed,batchSize=batchSize,specializeFeatures=request.specializeFeatures==true,subgroupSize=subgroupSize,blockingReadback=request.blockingReadback==true,retainRawReports=request.retainRawReports==true,retainSamples=request.retainSamples==true,candidates={}}
 local ids,maxDuration={},0
 for index,candidate in ipairs(request.candidates) do
  assert(type(candidate)=='table' and type(candidate.id)=='string' and candidate.id:match('%S') and not ids[candidate.id],'Candidate IDs must be unique and nonempty')
  ids[candidate.id]=true
  assert(type(candidate.packed)=='table' and #candidate.packed==layout.configWords,'Invalid packed configuration size')
  local packed={}
  for word=1,layout.configWords do
   local value=integer(candidate.packed[word],0,4294967295,'config word '..word)
   if layout.configTypes[word]=='f32' then assert(math.floor(value/8388608)%256~=255,'Nonfinite configuration float') end
   packed[word]=value
  end
  local endTime=packed[layout.endIndex]
  assert(endTime>=1000000 and endTime<=1800000000 and endTime%1000000==0,'Unsupported fight end time')
  local duration=endTime/1000000
  if candidate.duration~=nil then assert(candidate.duration==duration,'Duration does not match packed fight end') end
  maxDuration=math.max(maxDuration,duration)
  snapshot.candidates[index]={id=candidate.id,packed=packed,duration=duration}
 end
 snapshot.eventBudget=eventBudget or maxDuration*32+1024
 return snapshot
end
return M
