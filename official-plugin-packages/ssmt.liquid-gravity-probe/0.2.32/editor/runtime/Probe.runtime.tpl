namespace = LiquidGravityProbe
[Constants]
global $enabled = 1
global $emitting = 1
global $effect_requested = 1
global $last_effect_request = 1
global $shutdown_remaining = 0
global $toggle_pulse_request = 0
global $drain_elapsed = 0
global $seen = 0
global $initialized = 0
global $drawn = 0
global $manual_depth = 0
global $depth_test = 1
global $strength_level = 3
global $auto_pulse = 0
global $pose_probe = 0
global $anim_sync = 0
global $anim_sync_profile = 1
global $anim_fps = 60
global $anim_frames = 40
global $anim_segment_ramp = 10
global $anim_segment_burst = 20
global $anim_segment_after = 30
global $anim_drip_interval = 4
global $anim_ramp_interval = 2
global $anim_burst1_start = 20
global $anim_burst1_end = 22
global $anim_pressure_burst1 = 2.15
global $anim_splash_burst1 = 3
global $anim_burst2_start = 24
global $anim_burst2_end = 25
global $anim_pressure_burst2 = 1.85
global $anim_splash_burst2 = 2
global $anim_burst3_start = 27
global $anim_burst3_end = 28
global $anim_pressure_burst3 = 1.55
global $anim_splash_burst3 = 1
global $anim_pressure_drip = 0.92
global $anim_pressure_ramp_start = 1.05
global $anim_pressure_ramp_step = 0.03
global $nozzle_count = 1
global $nozzle_interval_0 = 0
global $nozzle_interval_1 = 0
global $nozzle_interval_2 = 0
global $nozzle_interval_3 = 0
global $particle_red = 0.78
global $particle_green = 0.51
global $particle_blue = 0.055
global $particle_pool = 1024
global $anim_phase = 0
global $anim_emit_gate = 1
global $anim_window = 0
global $anim_last_window = 0
[KeyToggle]
key = no_alt no_ctrl no_shift VK_NUMPAD1
run = CommandListToggleEffect
[CommandListToggleEffect]
if $effect_requested
    $effect_requested = 0
else
    $effect_requested = 1
endif
$toggle_pulse_request = $toggle_pulse_request + 1




[KeyAutoPulse]
key = no_alt no_ctrl no_shift VK_NUMPAD4
type = cycle
$auto_pulse = 0,1

[KeyStrengthUp]
key = no_alt no_ctrl no_shift VK_NUMPAD2
run = CommandListStrengthUp
[CommandListStrengthUp]
if $strength_level < 7
    $strength_level = $strength_level + 1
endif
[KeyStrengthDown]
key = no_alt no_ctrl no_shift VK_NUMPAD3
run = CommandListStrengthDown
[CommandListStrengthDown]
if $strength_level > 0
    $strength_level = $strength_level - 1
endif
[ResourceCamera]
[ResourceScene]
[ResourceDepth]
[ResourceSceneColor]
[ResourceFluidLighting]
[ResourceSavedComputeTexture0]
[ResourceSavedPixelDepth106]
[ResourceSavedPixelLight104]
[ResourceSavedComputeCB13]
[ResourceSavedPixelCB0]
[ResourceSavedPixelCB13]
[ResourceSavedVertexCB13]
[ResourceSelectedActor]
type = RWStructuredBuffer
stride = 16
array = 8
[TextureOverrideMainSceneDepth]
hash = 3a60b841
match_priority = 0
filter_index = 924172
; Main view depth is identified by its descriptor, not its map-dependent hash.
; Avoid symbolic division: fuzzy descriptor matches accept integer divisors,
; not a general command-list expression. Keep landscape, non-square depth
; candidates, then require the known main-view camera/scene layouts below.
; No exact swap-chain aspect ratio: dynamic resolution can round/pad dimensions.
[TextureOverrideMainViewDepth64Bit]
match_type = Texture2D
match_format = R32G8X24_TYPELESS
match_bind_flags = +depth_stencil
match_width = >height
match_height = >=180
match_msaa = 1
match_priority = -1
filter_index = 924172
[TextureOverrideMainViewDepth32Bit]
match_type = Texture2D
match_format = R32_TYPELESS
match_bind_flags = +depth_stencil
match_width = >height
match_height = >=180
match_msaa = 1
match_priority = -1
filter_index = 924172
[TextureOverrideMainViewDepth24Bit]
match_type = Texture2D
match_format = R24G8_TYPELESS
match_bind_flags = +depth_stencil
match_width = >height
match_height = >=180
match_msaa = 1
match_priority = -1
filter_index = 924172
[ShaderRegexCaptureCameraAndScene]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
if !$seen && od == 924172 && DRAW_TYPE % 2 == 0
    if vs-cb0->Size == 1536 && vs-cb1->Size == 3328
        ResourceCamera = copy vs-cb0->Region(vs-cb0->Offset, 1536)
        ResourceScene = copy vs-cb1->Region(vs-cb1->Offset, 3328)
        ResourceDepth = ref od
        $seen = 1
    elif vs-cb1->Size == 1536 && vs-cb2->Size == 3328
        ResourceCamera = copy vs-cb1->Region(vs-cb1->Offset, 1536)
        ResourceScene = copy vs-cb2->Region(vs-cb2->Offset, 3328)
        ResourceDepth = ref od
        $seen = 1
    endif
endif
; Render exclusively at the shared final scene stage below. The earlier
; map-specific PS hook could win $drawn before upscaling/detail passes.
[ShaderRegexRefreshMainViewDepth]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
if $seen && !$drawn && od == 924172 && DRAW_TYPE % 2 == 0
    if vs-cb0->Size == 1536 && vs-cb1->Size == 3328
        ResourceDepth = ref od
    elif vs-cb1->Size == 1536 && vs-cb2->Size == 3328
        ResourceDepth = ref od
    endif
endif
[ShaderOverrideCommonFinalSceneStage]
; Shared fullscreen VS in all three captured maps, independent of PS variant.
hash = 833324977c629596
allow_duplicate_hash = true
if $seen && $enabled && !$drawn && $\EFMIv1\enable_mods
    if ps-cb0->Size == 3328 && ps-cb1->Size == 512 && ps-t0 !== null
        $manual_depth = 1
        ResourceSceneColor = ref ps-t0
        run = CommandListInjectWorldInput
    endif
endif
[CommandListInjectWorldInput]
    ResourceSavedComputeTexture0 = ref cs-t0
    cs-t0 = null
    ps-t0 = null
    ResourceSavedPixelCB0 = copy ps-cb0->Region(ps-cb0->Offset,ps-cb0->Size)
    ResourceSavedComputeCB13 = ref cs-cb13
    ResourceSavedPixelLight104 = ref ps-t104
    ResourceSavedPixelDepth106 = ref ps-t106
    ResourceSavedPixelCB13 = ref ps-cb13
    ResourceSavedVertexCB13 = ref vs-cb13
    run = CommandListDrawWorld
    cs-cb13 = ref ResourceSavedComputeCB13
    ps-t104 = ref ResourceSavedPixelLight104
    ps-t106 = ref ResourceSavedPixelDepth106
    ps-cb0 = ref ResourceSavedPixelCB0
    ps-cb13 = ref ResourceSavedPixelCB13
    vs-cb13 = ref ResourceSavedVertexCB13
    ps-t0 = ref ResourceSceneColor
    cs-t0 = ref ResourceSavedComputeTexture0
[CommandListDrawWorld]
if $enabled && $seen && !$drawn && $\EFMIv1\enable_mods
    x241 = $surface_mode
    run = CustomShaderResolvePlayer
    if $pose_probe
    endif
    if !$initialized
        clear = ResourceFluidA
        clear = ResourceFluidB
        clear = ResourceFluidState
        clear = ResourceFluidSheetFrame
        clear = ResourceFocusMomentum
        clear = ResourceJetGuide
        clear = ResourcePulseState
        clear = ResourceAdhesionFrames
        clear = ResourceSplashA
        clear = ResourceSplashB
        clear = ResourceCollisionEvents
        clear = ResourceCollisionCounter
        clear = ResourceSplashFreeCounter
        clear = ResourcePuddleTransferCounter
        $initialized = 1
    endif
    ; Snapshot the completed live main-view depth at injection time.
    ; A compute-pass SRV may be an earlier surface or a stencil-plane view.
    ResourceCollisionDepth = copy ResourceDepth
    run = CommandListFluidFrame
    ResourceFluidLighting = copy ResourceSceneColor
    ResourceFluidDepth = copy ResourceDepth
    ; Use the same snapshot for collision and occlusion. Normalized UV lookup
    ; also supports final color targets whose size differs from native depth.
    y237 = $depth_test
    run = CustomShaderRenderFluidNoDepth
    $drawn = 1
endif
if !$seen
    $initialized = 0
endif
[Present]
; Map transitions/loading frames must not retain old map buffers.
if !$seen
    $initialized = 0
    ResourceCamera = null
    ResourceScene = null
    ResourceDepth = null
    ResourceNativeCollisionDepth = null
endif
; Key sections only change the request. Lifecycle commands run here, never in Key.
if $effect_requested != $last_effect_request
    $emitting = 1
    if $effect_requested
        $shutdown_remaining = 0
    else
        $shutdown_remaining = 8.0
    endif
    $last_effect_request = $effect_requested
endif
if $emitting
    if !$enabled
        $initialized = 0
    endif
    $enabled = 1
    $drain_elapsed = 0
endif
if !$\EFMIv1\enable_mods
    $initialized = 0
endif
post run = CommandListClearPose
post $seen = 0
post $drawn = 0
[CustomShaderResolvePlayer]
cs = ResolvePlayerAnchor.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-cb2 = ref ResourceScene
cs-u0 = ResourceSelectedActor
dispatch = 1,1,1
cs-u0 = null
[ResourcePoseInstances]
[ResourcePoseProbe]
type = RWStructuredBuffer
stride = 16
array = 786
[ShaderRegexCapturePose]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
if $pose_probe && DRAW_TYPE % 2 == 0 && od == 924172 && vs->srv_stride(0) == 16
    if vs-cb0->Size == 1536 && vs-cb1->Size == 3328 && vs-cb2->Size == 65536
        ResourcePoseInstances = ref vs-cb2->Region(vs-cb2->Offset,65536)
        ResourceScene = copy vs-cb1->Region(vs-cb1->Offset,3328)
        x245 = INSTANCE_COUNT
        y245 = FIRST_INSTANCE
        run = CustomShaderCapturePose
    endif
endif
[CustomShaderCapturePose]
cs = CapturePose.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-cb1 = ref ResourcePoseInstances
cs-cb2 = ref ResourceScene
cs-t0 = ref vs-t0
cs-u0 = ResourcePoseProbe
dispatch = 1,1,1
cs-u0 = null

[CommandListClearPose]
ResourceNativeCollisionDepth = null
clear = ResourcePoseProbe
clear = ResourceSurfaceAnchor
clear = ResourceSurfaceStats


[Constants]
global $surface_mode = 1

[ResourceSurfaceStats]
type = RWStructuredBuffer
stride = 16
array = 9
[ResourceSurfaceInstance]
[ResourceSurfaceProfiles]
type = RWStructuredBuffer
stride = 16
array = 12288
[ResourceSurfaceModelOffsets]
type = RWStructuredBuffer
stride = 16
array = 4096
[ResourceSurfaceEffects]
type = RWStructuredBuffer
stride = 16
array = 4096

[ResourceSurfaceAnchor]
type = RWStructuredBuffer
stride = 16
array = 48
[ResourceTrianglePositions]
type = Texture2D
format = R32G32B32A32_FLOAT
width = 3
height = 1
array = 1
mips = 1
msaa = 1
msaa_quality = 0
bind_flags = render_target shader_resource
[CommandListCaptureSurface]
run = CommandList\LiquidGravityProbe\JSONProfiles\Initialize
y242 = w243
if $surface_mode && $enabled && $seen && $\EFMIv1\enable_mods && od == 924172
    if vs-cb2->Size == 256
        ResourceSurfaceInstance = copy vs-cb2->Region(vs-cb2->Offset,256)
        w242 = 0
        run = CustomShaderCaptureTriangle
        run = CustomShaderResolveSurface
    elif vs-cb1->Size == 256
        ResourceSurfaceInstance = copy vs-cb1->Region(vs-cb1->Offset,256)
        w242 = 0
        run = CustomShaderCaptureTriangle
        run = CustomShaderResolveSurface
    elif vs-cb2->Size == 65536 && INSTANCE_COUNT == 1
        ResourceSurfaceInstance = copy vs-cb2->Region(vs-cb2->Offset,65536)
        w242 = FIRST_INSTANCE * 16
        run = CustomShaderCaptureTriangleInstanced
        run = CustomShaderResolveSurface
    elif vs-cb1->Size == 65536 && INSTANCE_COUNT == 1
        ResourceSurfaceInstance = copy vs-cb1->Region(vs-cb1->Offset,65536)
        w242 = FIRST_INSTANCE * 16
        run = CustomShaderCaptureTriangleInstanced
        run = CustomShaderResolveSurface
    endif
endif
[CustomShaderCaptureTriangle]
; Intentionally preserve the active game VS and all its skinning inputs.
gs = surface/CaptureTriangle.hlsl
ps = surface/CaptureTriangle.hlsl
hs = null
ds = null
cs = null
gs-cb13 = ref ResourceCamera
cull = none
depth_enable = false
blend = disable
topology = triangle_list
clear = ResourceTrianglePositions
run = BuiltInCommandListUnbindAllRenderTargets
o0 = set_viewport ResourceTrianglePositions
drawindexed = 3,y243,z243
[CustomShaderCaptureTriangleInstanced]
gs = surface/CaptureTriangle.hlsl
ps = surface/CaptureTriangle.hlsl
hs = null
ds = null
cs = null
gs-cb13 = ref ResourceCamera
cull = none
depth_enable = false
blend = disable
topology = triangle_list
clear = ResourceTrianglePositions
run = BuiltInCommandListUnbindAllRenderTargets
o0 = set_viewport ResourceTrianglePositions
drawindexedinstanced = 3,1,y243,z243,FIRST_INSTANCE
[CustomShaderResolveSurface]
cs = surface/ResolveSurface.hlsl
vs = null
ps = null
gs = null
hs = null
ds = null
cs-t0 = ref ResourceTrianglePositions
cs-t1 = ref ResourceSurfaceProfiles
cs-t2 = ref ResourceSurfaceModelOffsets
cs-t3 = ref ResourceSurfaceEffects
cs-cb13 = ref ResourceCamera
cs-cb1 = ref ResourceSurfaceInstance
cs-cb2 = ref ResourceScene
cs-u0 = ResourceSurfaceAnchor
cs-u1 = ResourceSurfaceStats
dispatch = 1,1,1
cs-u0 = null
cs-u1 = null
cs-t2 = null
cs-t3 = null


[Constants]
global $fluid_last_time = 0
global $fluid_dt = 0.0166667
[ResourceFluidA]
type = RWStructuredBuffer
stride = 80
array = __PRIMARY_COUNT__
[ResourceFluidB]
type = RWStructuredBuffer
stride = 80
array = __PRIMARY_COUNT__
[ResourceJetGuide]
type = RWStructuredBuffer
stride = 64
array = __PRIMARY_COUNT__
[ResourceFocusMomentum]
type = RWStructuredBuffer
stride = 16
array = __PRIMARY_COUNT__
[ResourceFluidSheetFrame]
type = RWStructuredBuffer
stride = 16
array = __PRIMARY_COUNT__
[ResourceFluidState]
type = RWStructuredBuffer
stride = 16
array = 4
[ResourcePrimaryNeighbors]
type = RWStructuredBuffer
stride = 32
array = __PRIMARY_COUNT__
[ResourceSplashNeighbors]
type = RWStructuredBuffer
stride = 32
array = __SPLASH_COUNT__
[CommandListFluidFrame]
$fluid_dt = time - $fluid_last_time
$fluid_last_time = time
if $fluid_dt <= 0 || $fluid_dt > 0.1
    $fluid_dt = 0.0166667
elif $fluid_dt > 0.0333333
    $fluid_dt = 0.0333333
endif
x238 = 0.012
y238 = 0.0000213333
z238 = 1000
w238 = 0.000001
x239 = $fluid_dt / 2
y239 = time - $fluid_dt / 2
z239 = 0
x250 = $nozzle_count
 x252 = $nozzle_interval_0
 y252 = $nozzle_interval_1
 z252 = $nozzle_interval_2
 w252 = $nozzle_interval_3
if !$effect_requested && $shutdown_remaining > 0
    $shutdown_remaining = $shutdown_remaining - $fluid_dt
    if $shutdown_remaining <= 0
        $emitting = 0
    endif
endif
if $anim_sync && $effect_requested
    $anim_phase = (time * $anim_fps) % $anim_frames
    $anim_window = 0
    if $anim_phase < $anim_segment_ramp
        $anim_emit_gate = 1
        z237 = $anim_pressure_drip
    elif $anim_phase < $anim_segment_burst
        $anim_emit_gate = 1
        z237 = $anim_pressure_ramp_start + ($anim_phase - $anim_segment_ramp) * $anim_pressure_ramp_step
    elif $anim_phase < $anim_segment_after
        $anim_emit_gate = 0
        z237 = $anim_pressure_drip
        if $anim_phase >= $anim_burst1_start && $anim_phase <= $anim_burst1_end
            $anim_window = 1
            $anim_emit_gate = 1
            z237 = $anim_pressure_burst1
        elif $anim_phase >= $anim_burst2_start && $anim_phase <= $anim_burst2_end
            $anim_window = 2
            $anim_emit_gate = 1
            z237 = $anim_pressure_burst2
        elif $anim_phase >= $anim_burst3_start && $anim_phase <= $anim_burst3_end
            $anim_window = 3
            $anim_emit_gate = 1
            z237 = $anim_pressure_burst3
        endif
    else
        $anim_emit_gate = 1
        z237 = $anim_pressure_drip
    endif
    x237 = $anim_emit_gate * $emitting
    w237 = $anim_emit_gate
    if $anim_window != $anim_last_window
        if $anim_window > 0
            $toggle_pulse_request = $toggle_pulse_request + 1
        endif
        $anim_last_window = $anim_window
    endif
else
    x237 = $emitting
    if $strength_level == 0
        z237 = 0.28
    else
        z237 = 0.565 + ($strength_level - 1) * 0.5625
    endif
    w237 = $manual_hold
endif
w239 = $auto_pulse
w240 = $toggle_pulse_request
z240 = $effect_requested
run = CustomShaderPulse
clear = ResourceCollisionCounter
clear = ResourceSplashFreeCounter
clear = ResourcePuddleTransferCounter
run = CustomShaderSplashUpdate
run = CommandListFluidSubstep
y239 = time
z239 = 1
run = CommandListFluidSubstep
run = CustomShaderSplashSpawn
run = CustomShaderFluidState
run = CustomShaderPreparePrimaryNeighbors
run = CustomShaderPrepareSplashNeighbors
; Main particles retire within .48s; retained surface droplets within another 1.6s.
; Count simulated time so a slow frame cannot cut surviving splashes short.
if !$emitting
    $drain_elapsed = $drain_elapsed + $fluid_dt
    if $drain_elapsed > 3.0
        $enabled = 0
        $initialized = 0
    endif
else
    $drain_elapsed = 0
endif
[CommandListFluidSubstep]
run = CustomShaderFluidPredict
run = CustomShaderFluidDensity
run = CustomShaderFluidCorrect
run = CustomShaderFluidDensity
run = CustomShaderFluidCorrect
run = CustomShaderFluidDensity
run = CustomShaderFluidCorrect
run = CustomShaderFluidFinalize

[CustomShaderFluidPredict]
cs = fluid/FluidPredict.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-t100 = ResourceFluidA
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-u5 = ResourceFluidSheetFrame
cs-u7 = ResourceJetGuide
cs-u6 = ResourceFocusMomentum
cs-u0 = ResourceFluidB
dispatch = __PRIMARY_DISPATCH__,1,1
cs-u0 = null
cs-u6 = null
cs-u7 = null
cs-u5 = null
cs-t100 = null
cs-t105 = null
cs-t109 = null

[CustomShaderFluidDensity]
cs = fluid/FluidDensity.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-t100 = ResourceFluidB
cs-t111 = ResourceFluidSheetFrame
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-u0 = ResourceFluidA
dispatch = __PRIMARY_DISPATCH__,1,1
cs-u0 = null
cs-t100 = null
cs-t111 = null
cs-t105 = null
cs-t109 = null

[CustomShaderFluidCorrect]
cs = fluid/FluidCorrect.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-t100 = ResourceFluidA
cs-t111 = ResourceFluidSheetFrame
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-u0 = ResourceFluidB
dispatch = __PRIMARY_DISPATCH__,1,1
cs-u0 = null
cs-t100 = null
cs-t111 = null
cs-t105 = null
cs-t109 = null

[CustomShaderFluidFinalize]
cs = fluid/FluidFinalize.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-t100 = ResourceFluidB
cs-t111 = ResourceFluidSheetFrame
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-cb13 = ref ResourceCamera
cs-t106 = ref ResourceCollisionDepth
cs-u2 = ResourceCollisionEvents
cs-u3 = ResourceCollisionCounter
cs-u6 = ResourceFocusMomentum
cs-u0 = ResourceFluidA
dispatch = __PRIMARY_DISPATCH__,1,1
cs-u0 = null
cs-u6 = null
cs-u2 = null
cs-u3 = null
cs-t106 = null
cs-t100 = null
cs-t111 = null
cs-t105 = null
cs-t109 = null

[CustomShaderFluidState]
cs = fluid/FluidState.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-t102 = ResourceSelectedActor
cs-t109 = ResourcePulseState
cs-u1 = ResourceFluidState
dispatch = 1,1,1
cs-u1 = null
cs-t109 = null

[CustomShaderPreparePrimaryNeighbors]
cs = fluid/NeighborPreparePrimary.hlsl
vs = null
ps = null
gs = null
hs = null
ds = null
cs-t100 = ResourceFluidA
cs-t103 = ResourceSurfaceAnchor
cs-t112 = ResourceJetGuide
cs-u0 = ResourcePrimaryNeighbors
dispatch = __PRIMARY_DISPATCH__,1,1
cs-u0 = null
cs-t100 = null
cs-t103 = null
cs-t112 = null

[CustomShaderPrepareSplashNeighbors]
cs = fluid/NeighborPrepareSplash.hlsl
vs = null
ps = null
gs = null
hs = null
ds = null
cs-t101 = ResourceSplashA
cs-t103 = ResourceSurfaceAnchor
cs-u0 = ResourceSplashNeighbors
dispatch = __SPLASH_DISPATCH__,1,1
cs-u0 = null
cs-t101 = null
cs-t103 = null

[CustomShaderRenderFluidDepth]
vs = fluid/FluidRenderColor.hlsl
ps = fluid/FluidRenderColor.hlsl
hs = null
ds = null
gs = null
cs = null
vs-cb13 = ref ResourceCamera
ps-cb13 = ref ResourceCamera
ps-t104 = ref ResourceFluidLighting
ps-t113 = ref ResourceSurfaceAnchor
vs-t100 = ResourceFluidA
vs-t101 = ResourceSplashA
vs-t102 = ResourceSelectedActor
vs-t112 = ResourceJetGuide
vs-t113 = ResourceSurfaceAnchor
vs-t114 = ResourcePrimaryNeighbors
vs-t115 = ResourceSplashNeighbors
cull = none
depth_enable = true
depth_write_mask = all
depth_func = greater_equal
blend = add src_alpha inv_src_alpha
topology = triangle_list
run = BuiltInCommandListUnbindAllRenderTargets
o0 = set_viewport ResourceSceneColor
od = ref ResourceFluidDepth
x251 = $particle_red
y251 = $particle_green
z251 = $particle_blue
draw = __RENDER_DRAW_COUNT__,0
vs-t100 = null
vs-t101 = null
vs-t102 = null
vs-t112 = null
vs-t113 = null
vs-t114 = null
vs-t115 = null
ps-t113 = null

[CustomShaderRenderFluidNoDepth]
vs = fluid/FluidRenderColor.hlsl
ps = fluid/FluidRenderColor.hlsl
hs = null
ds = null
gs = null
cs = null
vs-cb13 = ref ResourceCamera
ps-cb13 = ref ResourceCamera
ps-t104 = ref ResourceFluidLighting
ps-t106 = ref ResourceCollisionDepth
ps-t113 = ref ResourceSurfaceAnchor
vs-t100 = ResourceFluidA
vs-t101 = ResourceSplashA
vs-t102 = ResourceSelectedActor
vs-t112 = ResourceJetGuide
vs-t113 = ResourceSurfaceAnchor
vs-t114 = ResourcePrimaryNeighbors
vs-t115 = ResourceSplashNeighbors
cull = none
depth_enable = false
depth_write_mask = all
depth_func = greater_equal
blend = add src_alpha inv_src_alpha
topology = triangle_list
run = BuiltInCommandListUnbindAllRenderTargets
o0 = set_viewport ResourceSceneColor
od = ref ResourceFluidDepth
x251 = $particle_red
y251 = $particle_green
z251 = $particle_blue
draw = __RENDER_DRAW_COUNT__,0
vs-t100 = null
vs-t101 = null
vs-t102 = null
vs-t112 = null
vs-t113 = null
vs-t114 = null
vs-t115 = null
ps-t113 = null



[ResourceNativeCollisionDepth]
[ShaderOverrideCaptureNativeCollisionDepth]
; Validated depth SRV in the original scene pipeline.
hash = 6b4d0e4e401fa454
allow_duplicate_hash = true
if $seen && !$drawn && cs-t1 == 924172
    ResourceNativeCollisionDepth = ref cs-t1
endif
[ShaderOverrideCaptureSceneDepthStencilPair]
; All three new captures bind depth/stencil views of one resource to t0/t1.
; Select the depth plane at t0; do not overwrite it with the stencil plane.
hash = 1413562cb24aa8e2
allow_duplicate_hash = true
if $seen && !$drawn && cs-t0 == 924172 && cs-t1 == 924172
    ResourceNativeCollisionDepth = ref cs-t0
endif
[ResourceCollisionDepth]
; Preserve the native 64-bit depth/stencil resource format and compatible SRV.
[ResourceFluidDepth]
[ResourceCollisionEvents]
type = RWStructuredBuffer
stride = 48
array = __EVENT_CAPACITY__
[ResourceCollisionCounter]
type = RWBuffer
format = R32_UINT
array = 1
[ResourceSplashFreeCounter]
type = RWBuffer
format = R32_UINT
array = 1
[ResourcePuddleTransferCounter]
type = RWBuffer
format = R32_UINT
array = 3
[ResourceSplashA]
type = RWStructuredBuffer
stride = 80
array = __SPLASH_COUNT__
[ResourceSplashB]
type = RWStructuredBuffer
stride = 80
array = __SPLASH_COUNT__

[ResourceAdhesionFrames]
type = RWStructuredBuffer
stride = 64
array = __SPLASH_COUNT__
[CustomShaderSplashUpdate]
cs = fluid/SplashUpdate.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-cb13 = ref ResourceCamera
cs-t100 = ResourceSplashA
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-t106 = ref ResourceCollisionDepth
cs-t107 = ResourceCollisionCounter
cs-t108 = ResourceCollisionEvents
cs-u5 = ResourceAdhesionFrames
cs-u6 = ResourcePuddleTransferCounter
cs-u0 = ResourceSplashB
cs-u4 = ResourceSplashFreeCounter
dispatch = __SPLASH_DISPATCH__,1,1
cs-u0 = null
cs-u5 = null
cs-u6 = null
cs-u4 = null
cs-t100 = null
cs-t105 = null
cs-t109 = null
cs-t106 = null
cs-t107 = null
cs-t108 = null

[CustomShaderSplashSpawn]
cs = fluid/SplashSpawn.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-cb13 = ref ResourceCamera
cs-t100 = ResourceSplashB
cs-t102 = ResourceSelectedActor
cs-t103 = ResourceSurfaceAnchor
cs-t109 = ResourcePulseState
cs-t105 = ResourceFluidState
cs-t106 = ref ResourceCollisionDepth
cs-t107 = ResourceCollisionCounter
cs-t108 = ResourceCollisionEvents
cs-u5 = ResourceAdhesionFrames
cs-u6 = ResourcePuddleTransferCounter
cs-u0 = ResourceSplashA
cs-u4 = ResourceSplashFreeCounter
dispatch = __SPLASH_DISPATCH__,1,1
cs-u0 = null
cs-u5 = null
cs-u6 = null
cs-u4 = null
cs-t100 = null
cs-t105 = null
cs-t109 = null
cs-t106 = null
cs-t107 = null
cs-t108 = null

[Constants]
global $manual_hold = 0
[KeyManualPulse]
key = no_alt no_ctrl no_shift VK_NUMPAD0
type = hold
$manual_hold = 1
[ResourcePulseState]
type = RWStructuredBuffer
stride = 16
array = 6
[CustomShaderPulse]
cs = fluid/Pulse.hlsl
vs = null
ps = null
hs = null
ds = null
gs = null
cs-u0 = ResourcePulseState
dispatch = 1,1,1
cs-u0 = null

; Temporary native mesh diagnostics;
