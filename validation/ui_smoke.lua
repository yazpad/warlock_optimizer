-- Minimal benchmark workbench/backend guards, without display or GPU.
local draws=0
love={graphics={},event={quit=function() end}}
for _,name in ipairs({'clear','setColor','print','printf'}) do love.graphics[name]=function() draws=draws+1 end end
love.graphics.getWidth=function() return 1100 end
love.graphics.getRendererInfo=function() return 'OpenGL','4.6','test','mock GPU' end
love.getVersion=function() return 11,5,0 end
dofile('main.lua')
assert(not pcall(love.load,{}),'Normal startup must reject LÖVE 11.5')
assert(not pcall(love.load,{'--preview','--benchmark'}),'Preview cannot validate compute')
-- Reset main state before the independent preview.
dofile('main.lua');love.load({'--preview'});love.draw()
assert(draws>=5,'Workbench must render')
assert(not pcall(love.load,{'--unknown'}),'Unknown task options must fail')
print('PASS: minimal benchmark workbench, backend guards and CLI option rejection')
