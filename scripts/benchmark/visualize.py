import argparse
import matplotlib.pyplot as plt
import seaborn as sns
import pandas
import enum
import time


class CommandType(enum.Enum):
    BP_EVAL = "bp-eval"
    SIM_OVERHEAD = "sim-overhead"


def filter_lines(lines):
    lines = [line.strip() for line in lines]
    lines = [line for line in lines if len(line) > 0]
    return lines


def parse_bp_perf(filename):
    with open(filename) as f:
        lines = f.readlines()
        lines = filter_lines(lines)
    result = {"app": []}

    for line in lines:
        tokens = line.split(":")
        tokens = [a.strip() for a in tokens if a]
        if len(tokens) == 1:
            name = tokens[0]
            result["app"].append(name)
            continue
        assert len(tokens) == 2
        name = tokens[0]
        time = float(tokens[1][:-1])
        if name not in result:
            result[name] = []
        result[name].append(time)
    df = pandas.DataFrame(data=result)
    return df


def parse_time(filename, entry):
    with open(filename) as f:
        lines = f.readlines()
        lines = filter_lines(lines)

    result = {"Application": [], "Time": [], "Entry": []}

    for i in range(len(lines)):
        v = lines[i]
        if i % 2 == 0:
            result["Application"].append(v)
            result["Entry"].append(entry)
        else:
            t = time.strptime(v, "%M:%S.%f")
            t = t.tm_sec + t.tm_min * 60
            result["Time"].append(t)

    df = pandas.DataFrame(data=result)
    return df


def parse_overhead(names, filenames):
    df = None
    assert len(names) == len(filenames)
    for i in range(len(names)):
        r = parse_time(filenames[i], names[i])
        if df is None:
            df = r
        else:
            df = pandas.concat([df, r])
    return df


def visualize_bp_perf(df, filename):
    df["Other"] = df["eval loop"] - df["get_rtl_values"] - df["next breakpoints"] - df["eval breakpoint"]
    df = df[["app", "Other", "get_rtl_values", "next breakpoints", "eval breakpoint"]]
    df = df.rename(columns={"app": "Application", "get_rtl_values": "Get RTL Value", "next breakpoints": "Scheduler",
                            "eval breakpoint": "Expression Evaluation"})
    sns.set()
    df.set_index("Application").plot(kind="bar", stacked=True)
    plt.ylabel("Time (s)")
    plt.xlabel("Application")
    plt.title("Overhead breakdown when 8 breakpoints are inserted")
    plt.xticks(rotation=45)
    fig = plt.gcf()
    fig.set_size_inches(20, 10)
    fig.savefig(filename)


def visualize_performance_overhead(df, filename):
    sns.set()
    df.set_index("Application").plot(kind="bar", rot=0)
    plt.show()


def get_args():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", description="Plot benchmark result", required=True)
    parsers = {}
    commands = [v for v in CommandType]
    for command in commands:
        p = subparsers.add_parser(command.value)
        parsers[command] = p
        p.add_argument("-o", "--output", type=str, required=True)

    # command specific arguments
    parsers[CommandType.BP_EVAL].add_argument("input", type=str)
    parsers[CommandType.SIM_OVERHEAD].add_argument("-n", "--name", nargs="+", type=str, dest="names")
    parsers[CommandType.SIM_OVERHEAD].add_argument("files", nargs="+", type=str)

    return parser.parse_args()


def main():
    args = get_args()
    if args.command == CommandType.BP_EVAL.value:
        values = parse_bp_perf(args.input)
        visualize_bp_perf(values, args.output)
    elif args.command == CommandType.SIM_OVERHEAD.value:
        values = parse_overhead(args.names, args.files)
        visualize_performance_overhead(values, args.output)


if __name__ == "__main__":
    main()
