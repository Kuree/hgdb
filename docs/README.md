# hgdb: Hardware Generator Debugger
This documentation describes the design philosophy and usages of hgdb.

## What does hgdb do
There are several core features offered by hgdb:
- An infrastructure to create and query symbol table.
- A source level debugger server.
- A framework to interact with RTL simulators.

hgdb cannot do:
- IDE debugger. See supported debugger [here](https://github.com/Kuree/hgdb-debugger). Unless you want to debug with `curl` command, it's highly recommend to use an IDE-based debugger.
- Help you maintain symbol table in your hardware compiler. hgdb only offers means to create and query symbol table.

## How to target your hardware generator framework to hgdb
Before delving into the details on how hgdb stores symbol table, let's take a look at how hgdb works. hgdb implements the concept of "virtual breakpoint" that is distinct from many debuggers. In hgdb, a breakpoint does not have direct correlation to a particular RTL statement, hence the term "virtual". Nor does it requires special support from the simulator, e.g. `-line_debug` flag for Xcelium. It is emulated purely from the runtime given the symbol table. Such design allows hgdb to supports theoretically any simulator and any hardware generators that supports SSA! We will explain SSA in details.

The breakpoint in hgdb requires the following information:
- filename
- line number
- column number (optional)
- enable condition (optional)
- trigger condition (optional)

The first three fields are straightforward: these values correspond to the source-level information. If you want the ability to set breakpoint at a particular line in the source file, you need to provide these information.

### Enable condition
The enable condition is what's unique about hgdb and "virtual breakpoint". Informally this is the condition that fires the breakpoint. Practically speaking this is the SSA or `if` stack information. SSA stands for [static single assignment form](https://en.wikipedia.org/wiki/Static_single_assignment_form), which is commonly used in many software compilers. For combinational logic, the enable condition is essentially AND reduction of the SSA dominance frontier. Here is an example illustrating the idea (we will use this example throughout the documentation):

```
a := some_value
b := some_value
if (a) {            // line 3
    b := 0;         // line 4
} else {
    b := 1;         // line 6
}
a := b;             // line 7
```

After SSA transformation, we have something in SystemVerilog close to the following

```SystemVerilog
logic a, b, b_0, b_1

assign b_0 = 0;
assign b_1 = 1;
assign b_2 = a? b_0: b_1;
assign b = b_2;
assign a = b_2;
```

In the original source code, we have three lines that can be used to insert breakpoint: line 3, 4, and 6. Notice that after SSA transformation, we have generated additional signals which may be confusing to the designers. We will cover this later!

For line 3, since there is no conditional statement before the line, if we insert a breakpoint there, we would expect this to be triggered every clock cycle. In such case, we say the enable condition is `"1"`, since the expression always evaluates to `true`. In SSA analysis, the dominance frontier set for line 3 is empty. Similar rule applies to line 7 as well.

For line 4, the breakpoint will only be triggered if a is `true`. As a result, we put `a` as the enable condition. In SSA analysis, `a` is in the dominance frontier set of line 4.

Similarly, for line 5 we have `a == 0` as our enable condition and SSA analysis can be used to derive this.

For nested if statement in the original source code, we will have multiple elements in the dominance frontier set, hence we need to AND them together.

For sequential logic, however, it is much similar. Since SSA does not apply to sequential logic, you can generate the code block as usual:

```SystemVerilog
always_ff @(posedge clk, posedge rst) begin
    if (rst)
        data <= 0;   // line 3
    else
        data <= in;  // line 5
end
```

In this case, we can AND the if stack conditions. Line 3 will give `"rst"` and line 5 will give `"rst == 0"`.

Notice that we never specify which line of the generated RTL corresponds to the source code, nor do we worry about how many lines of code the source code gets generated to. Every breakpoint is defined from the source code.

### Trigger condition
The trigger condition only applies to combinational logic. It is designed to emulate the sensitivity as defined by the SystemVerilog LRM. If you want your breakpoint behaves the same way as SystemVerilog's `always_comb`, you need to compute the sensitivity list of your code block, and put the information as trigger condition. Otherwise, you can leave the field empty.

### Context scope information
One critical part of breakpoint is letting user inspect the variables defined in the current context. hgdb supports flexible and find-grained scope construction to meet requirements from different hardware generator frameworks. in hgdb, each breakpoint has its own scope and variables can be remapped differently across different breakpoints! Let's take a look at the example we see earlier:

```
a := some_value
b := some_value
if (a) {            // line 3
    b := 0;         // line 4
} else {
    b := 1;         // line 6
}
a := b;             // line 7
```

```SystemVerilog
logic a, b, b_0, b_1

assign b_0 = 0;
assign b_1 = 1;
assign b_2 = a? b_0: b_1;
assign b = b_2;
assign a = b_2;
```

At line 3, we have two variables, a, and b. Let's assume we will map `a` to RTL signal `a`, and `b` to RTL signal `b`. The scope looks something like this:
```JSON
{
    "a": "a",
    "b": "b"
}
```

Line 4 and 6 simply share the same scope information as line 3. In hgdb, we need to duplicated the scope information since the information is stored per breakpoint.

At line 7 however, value `b` has changed. If we track the renaming process of SSA, we can compute that `b` is actually holding `b2`'s value. In this case, we map `b` to `b2` and `a` to `a`, as shown below:
```JSON
{
    "a": "a",
    "b": "b_2"
}
```

So once we hit the breakpoint at line 7, hgdb will pull the value of `b_2` and represented as `b` and hide the fact that the symbol has been renamed via compiler passes!

hgdb even supports hierarchical mapping, that is, the mapped signal could be a nested child instance value generated by the compiler, e.g. `a -> inst1.a`, where `inst1` is a child instance of current generator.

### Generator values
Similar to the scope information, we can add attributes/fields to the generator object we are working on. Think of this as a `this` in C++ and `self` in Python. The mapping rules follow the same as the scope values except that the remapping happens per generator instance, not per breakpoint.

### Construct complex datatype
Source languages that support complex data type typically have to flatten the signals when producing RTL due to limited data type support in old Verilog. hgdb supports mapping from complex data type to RTL signal. Any supported IDE debugger is required to reconstruct the data type.

The process to store complex data type is straightforward, suppose we have the following `value` with such data type mapping:
```JSON
{
    "a": "signal1",
    "b": {
        "c": "signal2",
        "d": [
            "signal3", "signal4", "signal5", "signal6"
        ]
    }
}
```
Where each `signalX` is a flattened signal name. We can store them in such table form
```
value.a -> signal1
value.b.c -> signal2
value.b.d[0] -> signal3
value.b.d[1] -> signal4
value.b.d[2] -> signal5
value.b.d[3] -> signal6
```

For tagged union, you can generated different representation that points to the same signal. Since scope information is linked to each breakpoint, you can even switch to different representation in different lines!

## Interact with hgdb symbol table
TODO

## Breakpoint emulation loop
TODO
