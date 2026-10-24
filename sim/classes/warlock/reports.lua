local layout=require('sim.classes.warlock.generated.layout')
local hasFfi,ffi=pcall(require,'ffi')
if hasFfi then
 pcall(ffi.cdef,'typedef union { uint32_t u; float f; } fastsim_report_word;')
end
local M={}
local function finite(value) return value==value and value~=math.huge and value~=-math.huge end
local fieldListCache={}
local function getFieldList(mode)
 local list=fieldListCache[mode]
 if not list then
  local fields=assert(layout.fields[mode],'Unsupported report mode')
  list={}
  for name,field in pairs(fields) do
   if name~='done' and name~='highWater' then
    list[#list+1]={name=name,word=field.word,isFloat=field.type=='f32',isFrac=field.type=='fraction',part=field.part}
   end
  end
  fieldListCache[mode]=list
 end
 return list
end
function M.decode(data,lane,mode)
 local fields=assert(layout.fields[mode],'Unsupported report mode')
 local stride=mode=='fast' and layout.fastWords or layout.detailedWords
 local state={}
 if hasFfi and type(data)=='userdata' and data.getFFIPointer then
  local ptr=ffi.cast('const fastsim_report_word*',data:getFFIPointer())+lane*stride
  for name,field in pairs(fields) do
   local value=field.type=='f32' and tonumber(ptr[field.word].f) or tonumber(ptr[field.word].u)
   if field.part=='low' then value=value%65536 elseif field.part=='high' then value=math.floor(value/65536) end
   if field.type=='fraction' then value=value/65535 end
   assert(finite(value),'Nonfinite report field '..name)
   state[name]=value
  end
 else
  for name,field in pairs(fields) do
   local position=(lane*stride+field.word)*4+1
   local value=love.data.unpack(field.type=='f32' and 'f' or 'I4',data,position)
   if field.part=='low' then value=value%65536 elseif field.part=='high' then value=math.floor(value/65536) end
   if field.type=='fraction' then value=value/65535 end
   assert(finite(value),'Nonfinite report field '..name)
   state[name]=value
  end
 end
 assert(state.done==1,('Incomplete fight at batch lane %d (status %d)'):format(lane,state.done))
 assert(state.highWater<=layout.heapCapacity,'Reported event heap exceeds capacity')
 assert(state.total>=0,'Negative total damage')
 return state
end
function M.accumulateBatch(accumulators,data,completed,batchCount,iterations,eventBudget,mode)
 local stride=mode=='fast' and layout.fastWords or layout.detailedWords
 if hasFfi and type(data)=='userdata' and data.getFFIPointer then
  local base=ffi.cast('const fastsim_report_word*',data:getFFIPointer())
  local list=getFieldList(mode)
  for lane=0,batchCount-1 do
   local candidate=math.floor((completed+lane)/iterations)+1
   local a=accumulators[candidate]
   if a.samples then
    local state=M.decode(data,lane,mode)
    assert(state.events<=eventBudget,'Report exceeds submitted event budget')
    M.add(a,state)
   else
    local w=base+lane*stride
    local total=tonumber(w[0].f)
    local statusWord=tonumber(w[1].u)
    local done=statusWord%65536
    local highWater=math.floor(statusWord/65536)
    local events=tonumber(w[2].u)
    assert(done==1,('Incomplete fight at batch lane %d (status %d)'):format(lane,done))
    assert(highWater<=layout.heapCapacity,'Reported event heap exceeds capacity')
    assert(finite(total) and total>=0,'Invalid total damage')
    assert(events<=eventBudget,'Report exceeds submitted event budget')
    local dps=total/a.duration
    a.count=a.count+1
    local delta=dps-a.mean;a.mean=a.mean+delta/a.count;a.m2=a.m2+delta*(dps-a.mean)
    if highWater>(a.maxHeap or 0) then a.maxHeap=highWater end
    local totals=a.totals
    for i=1,#list do
     local f=list[i]
     local v=f.isFloat and tonumber(w[f.word].f) or tonumber(w[f.word].u)
     if f.part=='low' then v=v%65536 elseif f.part=='high' then v=math.floor(v/65536) end
     if f.isFrac then v=v/65535 end
     assert(finite(v),'Nonfinite report field '..f.name)
     totals[f.name]=(totals[f.name] or 0)+v
    end
   end
  end
  return
 end
 for lane=0,batchCount-1 do
  local candidate=math.floor((completed+lane)/iterations)+1
  local state=M.decode(data,lane,mode)
  assert(state.events<=eventBudget,'Report exceeds submitted event budget')
  M.add(accumulators[candidate],state)
 end
end
function M.accumulator(id,duration,retainSamples)
 return {id=id,duration=duration,count=0,mean=0,m2=0,totals={},samples=retainSamples and {} or nil}
end
function M.add(accumulator,state)
 local a=accumulator
 local dps=state.total/a.duration
 a.count=a.count+1
 local delta=dps-a.mean;a.mean=a.mean+delta/a.count;a.m2=a.m2+delta*(dps-a.mean)
 for key,value in pairs(state) do
  if key~='done' and key~='highWater' then a.totals[key]=(a.totals[key] or 0)+value end
 end
 a.maxHeap=math.max(a.maxHeap or 0,state.highWater)
 if a.samples then a.samples[#a.samples+1]=state end
end
function M.finish(a,expected)
 assert(a.count==expected,'Incomplete candidate samples')
 local sd=a.count>1 and math.sqrt(math.max(0,a.m2/(a.count-1))) or nil
 return {candidateId=a.id,status='complete',sampleCount=a.count,meanDps=a.mean,sd=sd,standardError=sd and sd/math.sqrt(a.count) or nil,
 totals=a.totals,maxHeap=a.maxHeap,samples=a.samples}
end
return M
