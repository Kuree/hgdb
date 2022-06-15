import argparse
import matplotlib.pyplot as plt
import seaborn as sns
import pandas


def parse_bp_perf(filename):
    with open(filename) as f:
        lines = f.readlines()
    result = {"app": []}
    data_frame = pandas.DataFrame()

    for line in lines:
        line = line.strip()
        if len(line) == 0:
            continue
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


def visualize_bp_perf(df, filename):
    df["Other"] = df["eval loop"] - df["get_rtl_values"] - df["next breakpoints"] - df["eval breakpoint"]
    df = df[["app", "Other", "get_rtl_values", "next breakpoints", "eval breakpoint"]]
    df = df.rename(columns={"app": "Application", "get_rtl_values": "Get RTL Value", "next breakpoints": "Scheduler",
                       "eval breakpoint": "Expression Evaluation"})
    print(df)
    sns.set()
    df.set_index("Application").plot(kind="bar", stacked=True)
    plt.ylabel("Time (s)")
    plt.xlabel("Application")
    plt.title("Overhead breakdown when 8 breakpoints are inserted")
    plt.xticks(rotation=45)
    fig = plt.gcf()
    fig.set_size_inches(20, 10)
    fig.savefig(filename)


def get_args():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", description="Plot benchmark result", required=True)
    for command in ["bp"]:
        p = subparsers.add_parser(command)
        p.add_argument("input", type=str)
        p.add_argument("-o", "--output", type=str, required=True)

    return parser.parse_args()


def main():
    args = get_args()
    if args.command == "bp":
        values = parse_bp_perf(args.input)
        visualize_bp_perf(values, args.output)


if __name__ == "__main__":
    main()
