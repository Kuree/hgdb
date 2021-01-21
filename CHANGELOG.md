# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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