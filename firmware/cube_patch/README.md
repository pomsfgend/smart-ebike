# cube_patch — minimal changes for the FGMDCharge cube

Files to **add** to `FGMDCharge-FD-ROUND-V2/main/` (the user's own repo; nothing here is copied from it):

| file | from |
|---|---|
| `fc_link.h`, `fc_link.c` | `../common/fc_common/` (shared protocol, host-tested) |
| `fc_cube_link.h`, `fc_cube_link.c` | this folder |

Then the ~25 lines of hooks in `sm_relay.c`, `main.c`, `config.h` and `CMakeLists.txt`
listed in `docs/INTEGRATION_WITH_SPOOF.md` §5. No pins, no I2C, no new hardware on the cube.

`gbt_tp_tx.[ch]` is an optional J1939 RTS/CTS sender, only needed if some station refuses the
cube's BAM transfers for BRM/BCP/BCS. It is not required for the link.

With no charger MAC configured, `fc_cube_link_init()` does nothing and the cube behaves exactly
as before (stock spoof).
