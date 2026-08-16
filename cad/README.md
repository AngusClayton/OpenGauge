# CAD files

## PCB

Open `PCB/daughter-board.kicad_pro` in KiCad. The project includes the
schematic and routed PCB.

The `PCB/production` directory contains generated outputs:

| File | Use |
| --- | --- |
| `daughter-board.zip` | PCB fabrication package |
| `bom.csv` | Generated PCB BOM |
| `positions.csv` | Component placement data |
| `designators.csv` | Reference list |
| `netlist.ipc` | IPC netlist |

Treat generated outputs as a matched set. If you change the schematic or PCB,
regenerate and review all production files. Do not mix files from different
exports. 

## Enclosure

The `Case` directory contains editable interchange and print files:

| File | Use |
| --- | --- |
| `OpenGauge.step` | Complete mechanical model |
| `mainBody.3mf` | Main enclosure print |
| `frontRing.3mf` | Front retaining ring print |

