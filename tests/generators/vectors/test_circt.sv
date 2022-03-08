// Standard header to adapt well known macros to our needs.
`ifdef RANDOMIZE_REG_INIT
  `define RANDOMIZE
`endif

// RANDOM may be set to an expression that produces a 32-bit random unsigned value.
`ifndef RANDOM
  `define RANDOM {$random}
`endif

// Users can define 'PRINTF_COND' to add an extra gate to prints.
`ifdef PRINTF_COND
  `define PRINTF_COND_ (`PRINTF_COND)
`else
  `define PRINTF_COND_ 1
`endif

// Users can define 'STOP_COND' to add an extra gate to stop conditions.
`ifdef STOP_COND
  `define STOP_COND_ (`STOP_COND)
`else
  `define STOP_COND_ 1
`endif

// Users can define INIT_RANDOM as general code that gets injected into the
// initializer block for modules with registers.
`ifndef INIT_RANDOM
  `define INIT_RANDOM
`endif

// If using random initialization, you can also define RANDOMIZE_DELAY to
// customize the delay used, otherwise 0.002 is used.
`ifndef RANDOMIZE_DELAY
  `define RANDOMIZE_DELAY 0.002
`endif

// Define INIT_RANDOM_PROLOG_ for use in our modules below.
`ifdef RANDOMIZE
  `ifdef VERILATOR
    `define INIT_RANDOM_PROLOG_ `INIT_RANDOM
  `else
    `define INIT_RANDOM_PROLOG_ `INIT_RANDOM #`RANDOMIZE_DELAY begin end
  `endif
`else
  `define INIT_RANDOM_PROLOG_
`endif

module ImplicitStateVendingMachine(	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:3:10
  input  clock, reset, io_nickel, io_dime,
  output io_dispense);

  wire       _io_dispense_output;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:3:10
  reg  [2:0] value;	// ImplicitStateVendingMachine.scala:11:22
  wire [2:0] incValue;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:18:5
  wire       doDispense;	// ImplicitStateVendingMachine.scala:13:26

  wire _GEN = ((io_nickel & io_dime) == 1'h0 | reset) == 1'h0;	// SimpleVendingMachine.scala:19:{9,10,22}
  always @(posedge clock) begin	// SimpleVendingMachine.scala:19:9
    `ifndef SYNTHESIS	// SimpleVendingMachine.scala:19:9
      if (`PRINTF_COND_ & _GEN)	// SimpleVendingMachine.scala:19:9
        $fwrite(32'h80000002, "Assertion failed: Only one of nickel or dime can be input at a time!\n    at SimpleVendingMachine.scala:19 assert(!(io.nickel && io.dime), \"Only one of nickel or dime can be input at a time!\")\n");	// SimpleVendingMachine.scala:19:9
      if (`STOP_COND_ & _GEN)	// SimpleVendingMachine.scala:19:9
        $fatal;	// SimpleVendingMachine.scala:19:9
    `endif
  end // always @(posedge)
  `ifndef SYNTHESIS	// ImplicitStateVendingMachine.scala:11:22
    `ifdef RANDOMIZE_REG_INIT	// ImplicitStateVendingMachine.scala:11:22
      reg [31:0] _RANDOM;	// ImplicitStateVendingMachine.scala:11:22

    `endif
    initial begin	// ImplicitStateVendingMachine.scala:11:22
      `INIT_RANDOM_PROLOG_	// ImplicitStateVendingMachine.scala:11:22
      `ifdef RANDOMIZE_REG_INIT	// ImplicitStateVendingMachine.scala:11:22
        _RANDOM = `RANDOM;	// ImplicitStateVendingMachine.scala:11:22
        value = _RANDOM[2:0];	// ImplicitStateVendingMachine.scala:11:22
      `endif
    end // initial
  `endif
      wire _GEN_0 = value >= 3'h4;	// ImplicitStateVendingMachine.scala:13:26
  assign doDispense = _GEN_0;	// ImplicitStateVendingMachine.scala:13:26
      wire [3:0] _GEN_1 = {1'h0, value} + {1'h0, incValue};	// ImplicitStateVendingMachine.scala:13:26, :18:20, SimpleVendingMachine.scala:19:10
  always @(posedge clock) begin	// ImplicitStateVendingMachine.scala:11:22
    if (reset)	// ImplicitStateVendingMachine.scala:11:22
      value <= 3'h0;	// ImplicitStateVendingMachine.scala:11:22
    else	// ImplicitStateVendingMachine.scala:11:22
      value <= _GEN_0 ? 3'h0 : _GEN_1[2:0];	// ImplicitStateVendingMachine.scala:18:{11,20}
  end // always @(posedge)
  assign incValue = io_dime ? 3'h2 : io_nickel ? 3'h1 : 3'h0;	// ImplicitStateVendingMachine.scala:11:22, :21:31, :22:29
  assign _io_dispense_output = _GEN_0;	// ImplicitStateVendingMachine.scala:24:15
  assign io_dispense = _io_dispense_output;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:3:10
endmodule

module SimpleVendingMachineTester(	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:37:10
  input clock, reset);

  wire       dut_io_dispense;	// SimpleVendingMachine.scala:73:19
  wire       _clock_wire;	// SimpleVendingMachine.scala:73:19
  wire       _reset_wire;	// SimpleVendingMachine.scala:73:19
  wire       _io_nickel_wire;	// SimpleVendingMachine.scala:73:19
  wire       _io_dime_wire;	// SimpleVendingMachine.scala:73:19
  reg  [3:0] cycle;	// Counter.scala:60:40
  wire       done;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:46:5
  wire       wrap_wrap;	// Counter.scala:72:24
  wire       nickelInputs_0;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_1;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_2;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_3;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_4;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_5;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_6;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_7;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_8;	// SimpleVendingMachine.scala:78:29
  wire       nickelInputs_9;	// SimpleVendingMachine.scala:78:29
  wire       dimeInputs_0;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_1;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_2;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_3;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_4;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_5;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_6;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_7;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_8;	// SimpleVendingMachine.scala:79:29
  wire       dimeInputs_9;	// SimpleVendingMachine.scala:79:29
  wire       expected_0;	// SimpleVendingMachine.scala:80:29
  wire       expected_1;	// SimpleVendingMachine.scala:80:29
  wire       expected_2;	// SimpleVendingMachine.scala:80:29
  wire       expected_3;	// SimpleVendingMachine.scala:80:29
  wire       expected_4;	// SimpleVendingMachine.scala:80:29
  wire       expected_5;	// SimpleVendingMachine.scala:80:29
  wire       expected_6;	// SimpleVendingMachine.scala:80:29
  wire       expected_7;	// SimpleVendingMachine.scala:80:29
  wire       expected_8;	// SimpleVendingMachine.scala:80:29
  wire       expected_9;	// SimpleVendingMachine.scala:80:29

  ImplicitStateVendingMachine dut (	// SimpleVendingMachine.scala:73:19
    .clock       (_clock_wire),	// SimpleVendingMachine.scala:73:19
    .reset       (_reset_wire),	// SimpleVendingMachine.scala:73:19
    .io_nickel   (_io_nickel_wire),	// SimpleVendingMachine.scala:73:19
    .io_dime     (_io_dime_wire),	// SimpleVendingMachine.scala:73:19
    .io_dispense (dut_io_dispense)
  );
  assign _clock_wire = clock;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:43:15
  assign _reset_wire = reset;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:44:15
  `ifndef SYNTHESIS	// Counter.scala:60:40
    `ifdef RANDOMIZE_REG_INIT	// Counter.scala:60:40
      reg [31:0] _RANDOM;	// Counter.scala:60:40

    `endif
    initial begin	// Counter.scala:60:40
      `INIT_RANDOM_PROLOG_	// Counter.scala:60:40
      `ifdef RANDOMIZE_REG_INIT	// Counter.scala:60:40
        _RANDOM = `RANDOM;	// Counter.scala:60:40
        cycle = _RANDOM[3:0];	// Counter.scala:60:40
      `endif
    end // initial
  `endif
      wire _GEN = cycle == 4'h9;	// Counter.scala:72:24
  assign wrap_wrap = _GEN;	// Counter.scala:72:24
      wire [4:0] _GEN_0 = {1'h0, cycle} + 5'h1;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, Counter.scala:72:24, :76:24
  always @(posedge clock) begin	// Counter.scala:60:40
    if (reset)	// Counter.scala:60:40
      cycle <= 4'h0;	// Counter.scala:60:40
    else	// Counter.scala:60:40
      cycle <= _GEN ? 4'h0 : _GEN_0[3:0];	// Counter.scala:76:24, :86:28
  end // always @(posedge)
  assign done = _GEN;	// Counter.scala:118:24
  assign nickelInputs_0 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_1 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_2 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_3 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_4 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_5 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:78:29
  assign nickelInputs_6 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:78:29
  assign nickelInputs_7 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:78:29
  assign nickelInputs_8 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:78:29
  assign nickelInputs_9 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:78:29
  assign dimeInputs_0 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_1 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_2 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_3 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_4 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_5 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:79:29
  assign dimeInputs_6 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:79:29
  assign dimeInputs_7 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_8 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:79:29
  assign dimeInputs_9 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:79:29
  assign expected_0 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_1 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_2 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_3 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_4 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:80:29
  assign expected_5 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_6 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_7 = 1'h1;	// Counter.scala:118:17, SimpleVendingMachine.scala:80:29
  assign expected_8 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
  assign expected_9 = 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:80:29
      wire [9:0] _GEN_1 = {{nickelInputs_9}, {nickelInputs_8}, {nickelInputs_7}, {nickelInputs_6}, {nickelInputs_5},
                {nickelInputs_4}, {nickelInputs_3}, {nickelInputs_2}, {nickelInputs_1}, {nickelInputs_0}};	// SimpleVendingMachine.scala:82:17
  assign _io_nickel_wire = _GEN_1[cycle];	// Counter.scala:72:24, SimpleVendingMachine.scala:82:17
      wire [9:0] _GEN_2 = {{dimeInputs_9}, {dimeInputs_8}, {dimeInputs_7}, {dimeInputs_6}, {dimeInputs_5},
                {dimeInputs_4}, {dimeInputs_3}, {dimeInputs_2}, {dimeInputs_1}, {dimeInputs_0}};	// SimpleVendingMachine.scala:83:15
  assign _io_dime_wire = _GEN_2[cycle];	// Counter.scala:72:24, SimpleVendingMachine.scala:83:15
      wire [9:0] _GEN_3 = {{expected_9}, {expected_8}, {expected_7}, {expected_6}, {expected_5}, {expected_4},
                {expected_3}, {expected_2}, {expected_1}, {expected_0}};	// SimpleVendingMachine.scala:84:26
      wire _GEN_4 = (dut_io_dispense == _GEN_3[cycle] | reset) == 1'h0;	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, Counter.scala:72:24, SimpleVendingMachine.scala:73:19, :84:{9,26}
  always @(posedge clock) begin	// SimpleVendingMachine.scala:76:21
    `ifndef SYNTHESIS	// SimpleVendingMachine.scala:76:21
      if (`STOP_COND_ & done & reset == 1'h0)	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:76:21
        $finish;	// SimpleVendingMachine.scala:76:21
      if (`STOP_COND_ & done & reset == 1'h0)	// /home/keyi/workspace/hgdb/tests/generators/vectors/test_chisel_firrtl.fir:47:13, SimpleVendingMachine.scala:76:{21,29}
        $finish;	// SimpleVendingMachine.scala:76:29
      if (`PRINTF_COND_ & _GEN_4)	// SimpleVendingMachine.scala:84:9
        $fwrite(32'h80000002, "Assertion failed\n    at SimpleVendingMachine.scala:84 assert(dut.io.dispense === expected(cycle))\n");	// SimpleVendingMachine.scala:84:9
      if (`STOP_COND_ & _GEN_4)	// SimpleVendingMachine.scala:84:9
        $fatal;	// SimpleVendingMachine.scala:84:9
    `endif
  end // always @(posedge)
endmodule

