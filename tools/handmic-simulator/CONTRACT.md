> Current wire audio is ADPCM only: formats=[2], 100 bytes, 160 samples/20ms. Internal browser microphone chunks remain 80 PCM samples.

# RF Link Passport Simulator — implementation contract

User requested a Mac BLE peripheral + Web replica of all Passport functions to diagnose choppy iOS audio; iOS code is not on this Mac, do not inspect/edit it. This is an authorized local diagnostic prototype. Do not modify/flash physical firmware or disturb its bond. Source main/hm_core.c,hm_product.c,hm_ui.c,hm_spectrum.c,hm_fragment.c reused READONLY. All web/native code here; dependencies/build output/settings in artifacts or Docker /build. No commit/push/vendor edits.

## Transport

Host Python server HTTP+WebSocket on127.0.0.1:8765. Swift macOS app uses URLSessionWebSocketTask ws://127.0.0.1:8765/bridge. Browser WS /ws. JSON objects in both directions. Only one native bridge, one local user. Backend stdin/stdout NDJSON; logs stderr. Server forwards protocol commands to backend, outputs bridge events or browser events as below. Server may wrap frontend metrics separately. No audio persisted automatically.

## Native → server → C backend

- `{"type":"link","connected":true,"central_id":"OS UUID","max_value":244,"subscribed":true}`; subscribed=true only all control/mic/flow notifications subscribed. Send changes, false if anysubscriptionlost; cap maxvalue bycentral.maximumUpdateValueLength. Native onecentral, reject another whileactive.
- `{"type":"ble_write","channel":"control|speaker|app_flow","hex":"lowercasehex"}` complete ATT value each; preserve callback timestamp as `arrival_ns` monotonic UInt64 nanoseconds (optional). CB write request ACK is ATT receipt only; businessreplyasync.
- `{"type":"bridge_state","state":"poweredOn|poweredOff|unauthorized|unsupported|advertising|error","message":"..."}` forwardedbrowser (backend mayignore).
Native Info/Status initial caches may be placeholder until backend supplies initial values, service exact below. Do not silently drop a write ifbridge socketdown; respond unlikelyError, surface bridge state. WebSocket reconnect bounded/backoff.

## Web/server → C backend

- `{"type":"button","key":"up|down|ok","event":"press|release"}` actual key edges; backend synthesizes releaseclick using product helper andticks forlong500/800ms. Web holds via pointer/key events, releases on blur/disconnect.
- `{"type":"mic","hex":"320hexchars"}` browser microphone80samples16LE/8k every10ms when enabled; backend honors core grant/flow/no credit. Do not fake microphone ifnotenabled.
- `{"type":"shutdown"}` safe exit. Optional `{"type":"reset"}` clears simulator session not physical device or OSbond.

## C backend → server

Each event output is JSON, no giant rawlogs. Server routes all to Web unless specified.
- `{"type":"characteristic","channel":"info|status","text":"validJSON"}` route nativecache too.
- `{"type":"ble_notify","channel":"control|mic|flow","hex":"..."}` route native. Control already fragmented 8Btransfer/offset/totalheader; nativequeue neverinterleave transfers. Native uses .indicate forctrl/.notify micflow, updateValue retry on canSend. Flowlatest queue coalescing, audio bounded120ms expiry; report droppedqueue stats.
- `{"type":"advertise","enabled":true}` route native. Coreproduct pairwindow/bond govern advertising. Native may advertise simulatorname whenenabled, sameformalservice. initiallynotopen untilWebOK toconnect, existingvirtualbond reconnectallowed.
- `{"type":"reset_link"}` route native: stopadvertise/recreate CBPeripheralManager/services if necessary to disconnect virtual central; do not remove realmacOSdevicebonds programmatically. Signal linkfalse/recreate state. Publish services again then obeylastadvertiseenabled.
- `{"type":"frame","hex":"RGB565 little-endian 240*320*2 bytes","width":240,"height":320}` framebuffer actualLVGL. Up to20Hz, onlyemit ifpixelchanged; largeframe uses512KBline limits. Python converts toPNG/stdlibzlib or JS rendersraw canvas.
- `{"type":"playback","hex":"PCM320bytes"}` simulated speaker 160 samples at 20ms, includesconcealmentzeros. Raw audio comparator independently decodes the 84-byte IMA block in each 100-byte BLE speaker value before backend acceptance.
- `{"type":"metrics","...":...}` at1Hz: corestats, sid/context/stream/direction/next_seq/grant_end, productvolume/theme/history, rxarrival/jitter whereavailable, buffer occupancy, accepted/rejected/underrun etc. Server can supplement arrivals/format checks.
- `{"type":"event","message":"..."}` product/link/errors/actions.

## GATT

Service9c3a0001-7a9b-4d9f-a15f-4d4f4c494e4b, chars0002 InfoREAD,0003ControlRXWRITErsp,0004ControlTXINDICATE,0005micNOTIFY,0006speakerWRITEnoRsp,0007DeviceFlowNOTIFY,0008AppFlowWRITEnoRsp,0009StatusREAD. Info/status dynamicread honorsoffset. Fullformalprotocol docs/spec/rf-link-handmic. Device name RF Link · SIM. Native security handledOS/peripheralcapability; don't claim hardwareSCpasskey/MITMenforcement replicated. Diagnostic replica logs virtualconnectionbinding; preservephysicalPassportbond. UI coverspairingmanagement, but sixdigitphysicalpasskey/OSbonding cannotfaithfullyinvent; label limitation Web prominently.

## C runtime behavior

Reuse core/product/UI/spectrum, no ESP/FreeRTOS. Real monotonic10ms loop with stdinselect; process boundedinput backlog andcontinueclock. hm_core_open when nativeconnected+allsubscriptions AND authorized byproductwindow or savedvirtualcentral. Savedvirtualsettingspath fromargv includesvolume/theme/virtualcentral ID, loadatomicprivatejsonfile. In simulator only, a successful app HELLO commitsvirtualbond, pendingwindowallowsinitialcoreopen. UI usesactual core/product snapshot and sameformatted fields asfirmware, hm_ui_set_phase; not rewrite inHTML. UIframes RGB565flush callback. Audio32slots using samecore acceptance/playbackclock with20msconsumer. HistoryonlyvalidacceptedPCM. Spectrum from consumed PCM or enabled mic. Matchingmicframe usescoreflow; notify controlfragments<=20beforeHELLO/value_maxafter. tickdrain<=4/controlreplies; DeviceFlow100active/500idle+creditadvance. coreerror/leases normal. Pairing window60s via model; clearvirtualbinding resetsownsession/persistentvirtualcentral only. Errorsstdin/JSON/badframe countednevercrash. stdinpipeEOF exit. No callbacks log voicePCM beyondtransportpipe.

## Build/launch

Cbackend built inSDKcontainer using existing LVGL managed sources + IDF cJSON, sourcecmake/lv_conf checkedinhere; output /build/handmic-simulator/backend. Root adds ./tools/dev simulator-build running build_backend.py. HostPythonserver spawns `docker compose run --rm --no-deps -T sdk /build/handmic-simulator/backend/passport_simulator /workspace/artifacts/handmic-simulator/settings.json`. SDKentrypointstdoutnoise mustfiltered nonJSON. Swift app native (CoreBluetoothrequiresMacOS) built usingexistingxcrunswiftc, nohostESPinstall; bundle outputs artifacts/handmic-simulator/RFLinkBLESimulator.app. Info.plist BluetoothusageNSBluetoothAlwaysUsageDescription + localnetworkATS. LaunchMacapp mayaskBluetoothpermission; usermustallowOSprompt. Browsermic/audio requiresusergesture. Root handles server/Web/launcher integration.

## Evidence

Simulator playback isdiagnostic, notphysicalCodec/RF/timingproof. Rawtap andreusedcoreplayback must separatelyvisible; MacreceivesdifferentBLEconnectionparameters fromC3 so comparisons supportlocalization, notabsoluteproofiOSinnocence. CPU/speakerclock simulated; no claim realfirmwareBSPjitter reproduced. NoiOScode/productionservers/physicaldeviceaccess.
