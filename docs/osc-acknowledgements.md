# OSC command acknowledgements

The updated TouchOSC layout uses an opt-in request envelope. Older direct addresses and `/sideshow/ping` remain supported unchanged. Older players ignore the new envelope, so install the updated player before using the new layout to control it.

## Connection

TouchOSC OSC Connection 1 sends UDP to the Pi on port 9000. Set its Receive port to 9001 and the layout's Reply port field to the same value. The field does not reconfigure TouchOSC's connection settings. Allow inbound replies on the Mac firewall. The player sends acknowledgements to the requesting host's IP and the explicitly requested reply port, from its OSC listening socket. Ping still replies to the source port.

Target `all` filters addresses; it does not discover devices or set the destination host. Broadcast delivery needs a subnet broadcast destination. Each reached panel replies independently. The UI lists responders and never claims that all panels received a broadcast.

## Request

Send `/sideshow/request` with:

| Argument | Type | Meaning |
|---|---|---|
| request ID | string | Nonempty, at most 64 bytes; unique per request |
| reply port | int32 | 1–65535 on the requesting host |
| command address | string | Original `/sideshow/<panel-id-or-all>/<command>` address, at most 1024 bytes |
| command arguments | original types | Zero to five int32, finite float32, or string arguments (strings at most 512 bytes) |

Example: `("stage-42", 9001, "/sideshow/all/fluid_persistence", 4.0)` requests 4× persistence. Argument count, command-specific types, bounds, and existing clamping rules are checked before enqueueing. Malformed routing headers and requests addressed to other panels are ignored. An addressed request with a valid routing header and invalid command/arguments receives `rejected`.

## Reply

`/sideshow/ack` carries four strings: request ID, panel ID, status, detail.

- `applied`: the main loop handled a synchronous state change. Detail includes the resulting setting, such as `fluid_persistence=4.000000` or a clamped `master=1.000000`. This confirms state application, not that a physical display has presented a frame.
- `accepted`: a valid request was handed to a backend, but completion is not proven. Used for asynchronous loading, navigation without a success result, and ISF changes awaiting presentation. No later completion acknowledgement is sent in this version.
- `rejected`: invalid arguments, unsupported control for the current mode, unavailable selection, synchronous initialization failure, or a full queue. Detail explains why.
- `superseded`: a queued master/blackout request was replaced by a newer value before application.

Settings retained for another mode (for example persistence while Video is active) can be applied without affecting the current picture. Errors from deferred loading can occur after `accepted`; check Ping and the Pi logs for the eventual selection.

## Controller behavior

The controller waits two seconds, correlates requests by ID and target, ignores duplicate/late/unknown replies, and bounds tracking to 128 requests with up to eight responding panels each. It shows the latest gesture; Fluid shortcuts show separate mode and preset results. Sliders remain coalesced to about 29 sends per second each. `No reply` means no matching response arrived within the deadline, not that the command definitely failed.

There are no automatic retries or server-side replay deduplication: replaying Next or an ISF event can execute it twice. Existing OSC clients can continue sending direct commands without acknowledgements. This protocol does not add authentication; use the same trusted network as the existing OSC controls.
