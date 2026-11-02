# FiveAxisCAM real-time protocols

`MachineIKTarget.fbs` (`F5IT`) and `MachineIKResult.fbs` (`F5IR`) own the wire contract. `generated/` contains the official `flatc` TypeScript output transpiled to browser ESM. `runtime/` is the repository's official FlatBuffers 25.12.19 TypeScript runtime, transpiled with its license retained. No package download is required.

Regenerate using Node 24 or newer:

```powershell
node src/apps/five-axis-cam/protocol/generate.mjs
```

Set `ICAX_FLATC` to select another existing official compiler binary. The default is `.codex_tmp/flatbuffers-v25.12.19/build/Release/flatc.exe`. Intermediate output is created in the OS temporary directory and removed after generation.

`machineIKPDOProtocol.mjs` exports `encodeMachineIKTarget({editToken, epoch, sequence, target})` and `decodeMachineIKResult(buffer)`. Matrices retain the schema's row-major double convention. Decoded unsigned 64-bit counters are decimal strings, with no precision loss. Decoded arrays are copied out of the lease. The decoder verifies table, vector, UTF-8 and byte bounds before invoking the generated accessors.

The protocol does not implement gesture lifecycle, transport fallback, collision rules, history or saving. Its caller owns current epoch/token checks, the latest-target slot, final acknowledgement and the reliable Begin/End boundary.

## Delivery semantics

PDO carries **high-frequency, disposable state**. A target means "the most recent desired pose", not "execute this motion command later". Both producer and consumer may skip intermediate versions. They must never accumulate a pointer queue, wait for each target acknowledgement before publishing the next target, or retry a busy slot through SDO.

Each gesture owns two single-writer slots: the renderer writes `MachineIKTarget`, and the scene writes `MachineIKResult`. Begin returns their explicit descriptors and the fixed ordering of joint/transform IDs. Each scene tick copies at most the latest input under a short read lease, releases that lease, then solves and checks the motion from the last accepted pose. Result publication is nonblocking and overwrites superseded results. Only matching epoch/token and increasing versions may update the display. The result contains actual accepted positions and world matrices, including following workpieces and fixtures; rejected targets retain the last valid pose.

SDO retains reliable, low-frequency intent: Begin, End/Cancel, numeric field commits, history and project saving. Pointer release first publishes the final target and awaits its processed-sequence acknowledgement through the result PDO. One End then closes the existing framework/database undo scope, followed by automatic persistence and one final applied View synchronization. A rejected final target is still acknowledged; it cannot be mistaken for an accepted pose. Cancel retires the slots and cancels that same scope. Slot versions restart only because a new gesture allocates new IDs. A stationary held drag needs no mailbox heartbeat.

## Other simulation interactions

| Interaction | Disposable state | Reliable boundary |
| --- | --- | --- |
| End-link drag | `MachineIKTarget` / `MachineIKResult` | Begin; final target ACK; End or Cancel |
| Held joint jog | `MachineJogControl` (`F5JC`) / `MachineJogResult` (`F5JR`) | Start; Stop returning exact final state |
| Machine planning, collision checking and local repair | `TaskProgress` (`F5TP`), no result/trajectory frames | Start; Cancel; one terminal QueryTask |
| Collider transforms, workpiece placement | Local draft and preview; no per-frame backend traffic | One validated commit after the gesture |
| Playback, seek, camera and occlusion | Local presentation of an already acquired plan/model | No per-frame command |

A joint jog is a velocity hold rather than a position target. It sends a new held-state version every 60 ms; reusing the same version cannot renew the 300 ms native dead-man lease. Native motion starts only after the first valid held snapshot. Missing input, release, collision or a limit stops movement. Stop is reliable and returns final accepted joint values and world matrices; no final position-target ACK is required. The entire hold remains one framework/database history entry. Other projects keep their readers; only this project's readers are paused while native PDO matrices drive its scene.

Task progress is published only when it changes. A terminal PDO snapshot signals that one reliable QueryTask may retrieve the complete result and retire its slot. Ordinary cancellation drains to that terminal state; an abandoned or disconnected progress consumer releases its slot explicitly. Captured-project persistence is completed even if that project has become inactive. Progress must never be reconstructed from a percentage or require per-frame mailbox queries.
