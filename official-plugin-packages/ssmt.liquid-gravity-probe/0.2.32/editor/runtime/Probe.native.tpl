[ResourceNativeCaptureCB0]
[ResourceNativeCaptureCB1]
[ResourceNativeCaptureCB2]
[ResourceNativeCaptureCB3]

; dump commands write only during frame analysis.
[Constants]
global $native_capture = 0
[ShaderRegexNativeMeshCapture]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
if $native_capture
    analyse_options = buf share_dupes persist
    if od == 924172 && DRAW_TYPE % 2 == 0 && vs-cb0->Size == 1536 && vs->srv_stride(0) == 16
        run = CommandListNativeUnmapped
    endif
endif
[CommandListNativeOriginal_m00_s00]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m00_s11]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s02]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s08]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s09]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s15]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s18]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m01_s19]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m02_s08]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m02_s15]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m03_s03]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m03_s08]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m03_s13]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m03_s16]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s00]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s01]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s04]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s12]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s13]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m04_s16]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s03]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s07]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s14]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s16]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s17]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s21]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s24]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s28]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m05_s30]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m06_s00]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m06_s02]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m06_s07]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m06_s09]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m07_s07]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m07_s08]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m07_s15]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m07_s16]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s02]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s03]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s06]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s17]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s18]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m08_s19]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m09_s03]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m09_s05]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m09_s20]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m09_s21]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m10_s02]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m10_s07]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m10_s13]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m10_s16]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m11_s04]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m11_s08]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m11_s19]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m11_s23]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m12_s06]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m12_s09]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m12_s14]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m12_s17]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m13_s00]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeOriginal_m13_s01]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[CommandListNativeUnmapped]
if $native_capture && od == 924172 && DRAW_TYPE % 2 == 0
    x246 = vb0->Offset
    y246 = vb0->Stride
    z246 = vb0->Size
    w246 = ib->Offset
    x247 = vb1->Offset
    y247 = vb1->Stride
    z247 = vb1->Size
    w247 = INDEX_COUNT
    x248 = vb2->Offset
    y248 = vb2->Stride
    z248 = vb2->Size
    w248 = FIRST_INSTANCE
    x249 = vb3->Offset
    y249 = vb3->Stride
    z249 = vb3->Size
    w249 = INSTANCE_COUNT
    x250 = vb4->Offset
    y250 = vb4->Stride
    z250 = vb4->Size
    w250 = ib->Stride
    run = CustomShaderNativeDrawBindings
    if vs-cb0->Size > 0 && vs-cb0->Size <= 65536
        ResourceNativeCaptureCB0 = copy vs-cb0->Region(vs-cb0->Offset,vs-cb0->Size)
    endif
    if vs-cb1->Size > 0 && vs-cb1->Size <= 65536
        ResourceNativeCaptureCB1 = copy vs-cb1->Region(vs-cb1->Offset,vs-cb1->Size)
    endif
    if vs-cb2->Size > 0 && vs-cb2->Size <= 65536
        ResourceNativeCaptureCB2 = copy vs-cb2->Region(vs-cb2->Offset,vs-cb2->Size)
    endif
    if vs-cb3->Size > 0 && vs-cb3->Size <= 65536
        ResourceNativeCaptureCB3 = copy vs-cb3->Region(vs-cb3->Offset,vs-cb3->Size)
    endif
    if vs->srv_stride(0) > 0 && vs->srv_stride(0) <= 256
    endif
    if vs->srv_stride(1) > 0 && vs->srv_stride(1) <= 256
    endif
    if vs->srv_stride(2) > 0 && vs->srv_stride(2) <= 256
    endif
    if vs->srv_stride(3) > 0 && vs->srv_stride(3) <= 256
    endif
    if vs->srv_stride(4) > 0 && vs->srv_stride(4) <= 256
    endif
    if vs->srv_stride(5) > 0 && vs->srv_stride(5) <= 256
    endif
    if vs->srv_stride(6) > 0 && vs->srv_stride(6) <= 256
    endif
    if vs->srv_stride(7) > 0 && vs->srv_stride(7) <= 256
    endif
    if vs->srv_stride(8) > 0 && vs->srv_stride(8) <= 256
    endif
    if vs->srv_stride(9) > 0 && vs->srv_stride(9) <= 256
    endif
    if vs->srv_stride(10) > 0 && vs->srv_stride(10) <= 256
    endif
    if vs->srv_stride(11) > 0 && vs->srv_stride(11) <= 256
    endif
    if vs->srv_stride(12) > 0 && vs->srv_stride(12) <= 256
    endif
    if vs->srv_stride(13) > 0 && vs->srv_stride(13) <= 256
    endif
    if vs->srv_stride(14) > 0 && vs->srv_stride(14) <= 256
    endif
    if vs->srv_stride(15) > 0 && vs->srv_stride(15) <= 256
    endif
endif

[ResourceNativeDrawBindings]
type = RWStructuredBuffer
stride = 16
array = 5
[CustomShaderNativeDrawBindings]
cs = native/CaptureBindings.hlsl
vs = null
ps = null
gs = null
hs = null
ds = null
cs-u0 = ResourceNativeDrawBindings
dispatch = 1,1,1
cs-u0 = null
