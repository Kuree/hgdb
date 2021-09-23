# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [0.0.4] - 2021009-23
### Added
- iverilog and Questa support
- Add ability to pause at clock edge
- Add timeout to recv_bp in python binding
- Add design info the debugger information request
- Add supppport for FSDB replay

### Changed
- Better out-of-order breakpoint support

### Fixed
- Fix index bug. Thanks Valgrind!
- Fix replay VPI emulator on new instance mapping

## [0.0.3] - 2021-02-18
### Added
- Automatically reuse built VCD table if exists
- Added extended vpi call to reverse time
- Add reverse continue
- Add more scheduling unit tests
- Add hex string to vpi and runtime
- Add support for arrays in vcd-replay
- Add vcd-rewrite-vcd to rewrite VCD based on symbol tables
- Add support for slice values
- Add support for set values
- Add tool to convert toml-based symbol table description to native hgdb database

### Changed
- Improve hierarchy mapping strategy (#15)
- Decouple debugger and scheduler logic
- VCD parser is refactor to be callback based
- Monitor proto is changed
- Add basename filename query support to relax absolute name requirement

### Fixed
- Fix hgdb-replay script args passing
- Fix scope resolution with VPI
- Fixed a bug where VCD will have extra records if a value changed multiple times in the same timestamp
- Variable minor bug fixes in vcd-replay

## [0.0.2] - 2021-02-03
### Added
- Add monitor request
- Add cb lock to prevent race condition
- Add initial implementation of hgdb-replay
- Add tcl binding
- Add step back within the timestamp. If the simulator/emulator is capable, rewind time

### Changed
- Response are sent to requesting channel only, except for breakpoint broadcast
- Eval request now requires context indicator
- Use shallow copy to avoid client remove callbacks during iteration

### Fixed
- Fix a bug where context symbol won't be read properly during bp eval
- fix a bug where if the db is invalid, subsequent logic will trigger a seg fault
- Fix a race-condition bug where the compiler/cpu reordering may introduce unexpected side-effects. Add memory fence to avoid reordering
- Fix a typo in error message
- Fix stop/detach logic
- Fix a bug where cb handles is not properly added to the handle list 

## [0.0.1] - 2021-01-21
Initial release. This is a complete rewrite from the old version
[kratos-runtime](https://github.com/Kuree/kratos-runtime) with various improvement. The list below shows the major
difference between hgdb and its predecessor.
### Added
- Websocket + JSON based communication
- Improved API on different requests and responses
- Language/framework independent symbol table design
- Breakpoint emulation instead of DPI calls
- Python bindings to interact with the symbol table, as well as libhgdb
- A new expression evaluation engine for C-like expressions
- Add REPL support
- Better testing infrastructure
