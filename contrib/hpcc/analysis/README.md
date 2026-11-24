# HPCC analysis tools

Build the C++17 tools with `make` in this directory. Use `make OUT=/path/to/bin` to build in a separate directory. The reader shares the simulator's trace and settings headers.

- `./trace_reader FILE.tr [FILTER]` prints packet events. For example, `'node=0&(l3Prot=0x11&ecn!=0)'` selects ECN-marked UDP packets recorded on node 0.
- `./fct_analysis -p /path/to/mix/fct_topology_flow -c hp95,dcqcn -s 5` compares per-size-bin FCT slowdowns. `python3 fct_analysis.py` accepts the same options and runs with the Python standard library.
- Use `-S FILE` with the output of `traffic_gen/flow_bins.py` to compare schemes over identical byte ranges.

See the [operations manual](../../../HPCC_OPERATIONS.md) for commands using simulator outputs, filters and column definitions.
