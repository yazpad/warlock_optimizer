function love.conf(t)
  t.identity = 'fastsim'
  t.window.title = 'Fastsim — Vulkan benchmark'
  t.window.width, t.window.height = 1100, 420
  t.window.minwidth, t.window.minheight = 800, 320
  t.window.resizable = true
  t.window.vsync = 0
  t.graphics = t.graphics or {}
  t.graphics.renderers = {'vulkan'}
  t.modules.audio = false
  t.modules.physics = false
end
