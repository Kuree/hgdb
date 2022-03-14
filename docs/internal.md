# How hgdb works

This documentation describes a shortened version of our [paper](https://arxiv.org/abs/2203.05742) (DAC'22).

## System architecture
<figure markdown>
  ![hgdb system diagram](https://github.com/Kuree/files/raw/master/images/hgdb-system-diagram.svg)
  <figcaption>hgdb system diagram</figcaption>
</figure>

hgdb defines three sets of interface, as shown in the figure above

- Simulator
- Debugger
- Symbol table

Most interface are implemented in RPC (either TCP or websocket) except for the simulator interface, which is
implemented in Verilog Programming Interface (VPI) and linked directly by the simulator.

## How breakpoints are emulated
Breakpoints are emulated inside the runtime. At each clock edge, hgdb "briefly" pauses the simulation and evaluates 
each inserted breakpoint. If any inserted breakpoints is evaluated triggered under the current simulation state, hgdb
sends corresponding events to the debugger and waits for users command. If none of the breakpoints can be triggered,
hgdb unpauses the simulation and wait until the next clock edge.

The evaluation loop is shown below. Notice that we can evaluate the breakpoints in reversed order to implement
reverse-debugging.

<figure markdown>
  ![hgdb breakpoint emulation loop](https://github.com/Kuree/files/raw/master/images/hgdb-breakpoint-schedule.svg)
  <figcaption>hgdb breakpoint emulation loop</figcaption>
</figure>


## Symbol table structure
hgdb supports both SQLite and JSON-based symbol table. It's recommended to use JSON since it is more flexible and
human-readable. To see how the JSON-based is structured, please check out the JSON schema
[here](https://github.com/Kuree/hgdb/blob/docs/include/schema.json).

TCP/Websocket-based symbol table is also supported. Users need to talk to the debugger using protocol specified in the
[section below](#debugger-protocol).

## Debugger protocol
This section describes debugger protocol used in hgdb. The protocol itself is implemented in WebSocket, which maximizes
compatibility with different usage cases such as web browsers.

(work in progress).

