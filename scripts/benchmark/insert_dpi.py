import argparse


def rewrite_rtl(input_file):
    result = ['import "DPI-C" function void HGDB_DEBUG_DPI(int a, int b);\n']

    with open(input_file) as f:
        lines = f.readlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        # transform only if there is a line command
        if "assign " not in line or "@" not in line:
            result.append(line)
            continue
        result.append("always_comb begin\n")
        # compute leading space to make it looks nicer
        num_space = len(line) - len(line.lstrip())
        result.append(" " * num_space + "HGDB_DEBUG_DPI(0, {0});\n".format(i))
        result.append("end\n")
        result.append(line)
        while ';' not in line:
            line = lines[i]
            i += 1
            result.append(line)
    return result


def output_rtl(lines, output_file):
    with open(output_file, "w+") as f:
        f.writelines(lines)


def main():
    parser = argparse.ArgumentParser("insert_dpi")
    parser.add_argument("input", help="Verilog input")
    parser.add_argument("output", help="Verilog output")

    args = parser.parse_args()
    input_file = args.input
    output_file = args.output

    lines = rewrite_rtl(input_file)
    output_rtl(lines, output_file)


if __name__ == "__main__":
    main()