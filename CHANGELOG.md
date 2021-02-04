# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.2] - 2021-02--3
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