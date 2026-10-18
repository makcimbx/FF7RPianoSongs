; Standard Windows x64 leaf tail dispatch: no stack/register argument movement,
; no C++ call, initialization, waits or original replay. R11/flags are volatile.
; An unpublished table or unsupported union export terminates deterministically.
OPTION CASEMAP:NONE
EXTERN native_proxy_targets:QWORD
.code
PUBLIC native_proxy_fail_fast
native_proxy_fail_fast PROC
    mov ecx, 7 ; FAST_FAIL_FATAL_APP_EXIT
    int 29h
native_proxy_fail_fast ENDP

FORWARD MACRO label_name, slot
PUBLIC label_name
label_name PROC
    mov r11, qword ptr [native_proxy_targets]
    test r11, r11
    jz native_proxy_fail_fast
    jmp qword ptr [r11 + slot * 8]
label_name ENDP
ENDM

FORWARD proxy_ord_1, 0
FORWARD proxy_ord_2, 1
FORWARD proxy_ord_3, 2
FORWARD proxy_ord_4, 3
FORWARD proxy_ord_5, 4
FORWARD proxy_ord_6, 5
FORWARD proxy_ord_7, 6
FORWARD proxy_ord_8, 7
FORWARD proxy_ord_9, 8
FORWARD proxy_ord_10, 9
FORWARD proxy_ord_11, 10
FORWARD proxy_ord_12, 11
FORWARD proxy_ord_13, 12
FORWARD proxy_ord_14, 13
FORWARD proxy_ord_15, 14
FORWARD proxy_ord_16, 15
FORWARD proxy_ord_17, 16
FORWARD proxy_ord_18, 17
FORWARD proxy_ord_19, 18
FORWARD proxy_ord_20, 19
FORWARD proxy_ord_21, 20
FORWARD proxy_ord_22, 21
FORWARD proxy_ord_23, 22
FORWARD proxy_ord_24, 23
FORWARD proxy_ord_25, 24
FORWARD proxy_ord_26, 25
FORWARD proxy_ord_27, 26
FORWARD proxy_ord_28, 27
FORWARD proxy_ord_29, 28
FORWARD proxy_ord_30, 29
FORWARD proxy_ord_31, 30
FORWARD proxy_ord_32, 31
FORWARD proxy_ord_33, 32
FORWARD proxy_ord_34, 33
FORWARD proxy_ord_35, 34
FORWARD proxy_ord_36, 35
FORWARD proxy_ord_37, 36
FORWARD proxy_ord_38, 37
FORWARD proxy_ord_100, 38
FORWARD proxy_ord_101, 39
FORWARD proxy_ord_102, 40
FORWARD proxy_ord_103, 41
FORWARD proxy_D3DAssemble, 42
FORWARD proxy_DebugSetMute, 43
FORWARD proxy_D3DCompile, 44
FORWARD proxy_D3DCompressShaders, 45
FORWARD proxy_D3DCreateBlob, 46
FORWARD proxy_D3DDecompressShaders, 47
FORWARD proxy_D3DDisassemble, 48
FORWARD proxy_D3DDisassemble10Effect, 49
FORWARD proxy_D3DGetBlobPart, 50
FORWARD proxy_D3DGetDebugInfo, 51
FORWARD proxy_D3DGetInputAndOutputSignatureBlob, 52
FORWARD proxy_D3DGetInputSignatureBlob, 53
FORWARD proxy_D3DGetOutputSignatureBlob, 54
FORWARD proxy_D3DPreprocess, 55
FORWARD proxy_D3DReflect, 56
FORWARD proxy_D3DReturnFailure1, 57
FORWARD proxy_D3DStripShader, 58
FORWARD proxy_Direct3DCreate9On12, 59
FORWARD proxy_Direct3DCreate9On12Ex, 60
FORWARD proxy_Direct3DShaderValidatorCreate9, 61
FORWARD proxy_PSGPError, 62
FORWARD proxy_PSGPSampleTexture, 63
FORWARD proxy_D3DPERF_BeginEvent, 64
FORWARD proxy_D3DPERF_EndEvent, 65
FORWARD proxy_D3DPERF_GetStatus, 66
FORWARD proxy_D3DPERF_QueryRepeatFrame, 67
FORWARD proxy_D3DPERF_SetMarker, 68
FORWARD proxy_D3DPERF_SetOptions, 69
FORWARD proxy_D3DPERF_SetRegion, 70
FORWARD proxy_DebugSetLevel, 71
FORWARD proxy_Direct3D9EnableMaximizedWindowedModeShim, 72
FORWARD proxy_Direct3DCreate9, 73
FORWARD proxy_Direct3DCreate9Ex, 74
FORWARD proxy_D3D11CreateDevice, 75
FORWARD proxy_D3D11CreateDeviceAndSwapChain, 76
FORWARD proxy_DirectSoundCreate8, 77
FORWARD proxy_DirectSoundCaptureEnumerateW, 78
FORWARD proxy_DirectSoundCaptureCreate8, 79
FORWARD proxy_timeGetDevCaps, 80
FORWARD proxy_timeEndPeriod, 81
FORWARD proxy_timeGetTime, 82
FORWARD proxy_timeBeginPeriod, 83
FORWARD proxy_CreateDXGIFactory, 84
FORWARD proxy_CreateDXGIFactory1, 85
FORWARD proxy_CreateDXGIFactory2, 86
FORWARD proxy_XInputGetState, 87
FORWARD proxy_XInputSetState, 88
FORWARD proxy_XInputGetCapabilities, 89
FORWARD proxy_XInputEnable, 90
FORWARD proxy_XInputGetDSoundAudioDeviceGuids, 91
FORWARD proxy_XInputGetBatteryInformation, 92
FORWARD proxy_XInputGetKeystroke, 93
END
