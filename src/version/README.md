# Version-specific protocol layer

`ASCEMU_VERSION` selects both the core client profile and the login/server topology.

- `Classic`, `TBC`, `WotLK`, `Cata`, `Mop`: legacy `logonserver` path.
- `Midnight`: Battle.net + the implemented Midnight World V2 protocol profile.
- `Forever`: Battle.net automatically, with an independent protocol profile under `version/Forever`.

There is deliberately no user-facing `BUILD_BATTLENET` option. A modern client version always requires Battle.net, so CMake derives that setting from `ASCEMU_VERSION`.

Build-specific opcodes, GUID layouts, packet serializers and authentication material belong inside their version directory and must not leak into generic core code.

## Opcode and packet architecture

Modern profiles follow the same separation used by the legacy AscEmu world protocol:

1. `Opcodes.hpp` defines stable internal semantic opcode ids.
2. `OpcodeTable.cpp` maps the active client's 32-bit wire opcodes to those ids.
3. `OpcodeHandlerRegistry` maps internal ids to handlers and validates session state.
4. `Packets/ManagedPacket.hpp` provides the same serialize/deserialize pattern as the legacy managed packets.
5. `World/OpcodeHandler.cpp` is transport-only: decrypt, map raw opcode, construct a packet, dispatch.
6. Domain handlers live under `Packets/Handlers/` instead of accumulating inside `WorldSocket.cpp`.

This deliberately keeps the modern 32-bit opcode framing separate from the legacy `WorldPacket` (`uint16_t`) while retaining AscEmu's handler model.
