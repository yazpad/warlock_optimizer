local R={}
function R.inspect(preview)
 local major,minor,revision=love.getVersion()
 local name,version,vendor,device=love.graphics.getRendererInfo()
 local ready=major>=12 and name:lower():find('vulkan',1,true)~=nil
 return {ready=ready,preview=preview,version=('%d.%d.%d'):format(major,minor,revision),backend=name,device=device,vendor=vendor,
 message=ready and 'Vulkan ready · combat validation pending' or 'Requires LÖVE 12 with Vulkan. UI preview only.'}
end
return R
