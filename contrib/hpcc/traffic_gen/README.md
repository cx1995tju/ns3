# HPCC traffic generator

Run `python3 traffic_gen.py -h` for options. Each host generates Poisson arrivals, samples flow sizes from a linearly interpolated CDF, and picks another host uniformly. Output starts at simulation time 2 s and uses the simulator's six-column flow format.

- `WebSearch_distribution.txt` and `FbHdp_distribution.txt` are the workloads used in the HPCC paper.
- `AliStorage2019.txt` describes Alibaba distributed storage traffic collected in 2019.
- `GoogleRPC2008.txt` describes Google's RPC size distribution before 2008.

Use `python3 flow_bins.py -c CDF_FILE` to print byte limits and cumulative percentages for FCT analysis. The [operations manual](../../../HPCC_OPERATIONS.md) includes a complete example for the bundled topology.
