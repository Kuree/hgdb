# hgdb debuggers
This documentation contains a collections of hgdb debuggers.


## Visual Studio Code
This debugger is the reference implementation of an IDE-based debugger.
To install, simply use the following command in the VS code console:

```
ext install keyiz.hgdb-vscode
```

Users should expect the same debugging experience as debugging any program in Visual Studio Code as
it implements the majority of the adapter protocol.

Supported Features:

- Set/remove breakpoints
- REPL
- Multiple instances view
- Complex data type rendering
- Reverse debugging
- Data breakpoint (watchpoints)

To use the debugger, simply press <key>F5</key> and choose `HGDB debug`.

Below is a quick overview of its interface using Rocket-Chip as an example:


![type:video](https://user-images.githubusercontent.com/6099149/136262887-8ee63329-4bb7-4372-81ab-f06411064926.mp4)

You can check out the sample `launch.json`
[here](https://github.com/Kuree/hgdb/blob/master/tests/generators/.vscode/launch.json),
which provides an example of debugger configuration. Notice that your current working
directory must contain the source code, otherwise the extension will run into errors when trying to open up the
file upon breakpoint.


!!! note

    Notice that if the simulation is running on a remote server, the symbol table path has to be remote and
    absolute! In addition, if your working directory is local (not mounted), you need to provide source mapping
    in `launch.json`.
    Check the [configuration setup](https://github.com/Kuree/hgdb-debugger/blob/master/vscode/src/extension.ts#L61-L66)
    for more details.

## Console
The console version is implemented in Python and mimics the style of `gdb`.
It uses built-in Python-bindings to communicate with the `hgdb` runtime.
To install this debugger, simply do

```
$ pip install hgdb-debugger
```

Most of the commands are identical to those of `gdb`. Type `help` to see a list of commands.

Below is an example usage, where we connect to a localhost with port number `8888`. The symbol file is `debug.db`.

```
$ hgdb localhost:8888 debug.db
```

Supported Features:

- Set/remove breakpoints
- REPL!
- Auto complete and suggestion
- Pretty print on complex data type
- Reverse debugging
- Data breakpoint (watchpoints)

Here is a rendered `asciinema` of the hgdb console debugger when debugging simulation with Xcelium (command argument
may be different due to version changes.):


![SVG of hgdb-console](https://rawcdn.githack.com/Kuree/files/29a6a3c427b46755be29cb513388112490c89ba5/images/hgdb-console.svg)

If the symbol table is compiled on a different machine where the simulator runs, but the source code is local,
you need to specify the file  mapping using `--map`. An example would be `--map /remote/dir:/local/dir`,
where the first dir is remote src and the second is the local src.

Notice that for Chisel users, you need to specify a working directly so that the debugger can locate the source files.
This is because Chisel only encodes the basename of a file, which makes it impossible to resolve without a working
directory as a reference. You can use `--dir [folder]` to specify it. You can also use `--map` to remap the filename,
e.g. `--map :/absolute/dir` by providing an empty remote dir.

!!! note

    Notice that if the simulation is running on a remote server, the symbol table path has to be remote and
    absolute! In addition, if your working directory is local (not mounted), you need to provide source mapping
    in `launch.json`.
    Check the [configuration setup](https://github.com/Kuree/hgdb-debugger/blob/master/vscode/src/extension.ts#L61-L66)
    for more details.

