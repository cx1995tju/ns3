HPCC datacenter simulator
=========================

This module ports the RDMA simulator from
`High-Precision-Congestion-Control <https://github.com/alibaba-edu/High-Precision-Congestion-Control>`_
(revision ``9f4be2a``) to ns-3.49. A host runs an ``RdmaClient`` for each flow.
Its ``RdmaDriver`` assigns a queue pair to a NIC, and ``RdmaHw`` controls pacing,
ACK/NACK processing and rate updates. Switches forward using ECMP, update INT
on dequeue, mark ECN at the egress port and send PFC on the ingress port.
The DCTCP mode uses the upstream hardware model with line-rate startup
and RDMA acknowledgments.

Build and run
-------------

Run these commands from the ns-3 source root.

.. code-block:: bash

   ./ns3 configure --enable-modules=hpcc --enable-tests
   ./ns3 build scratch/hpcc/third test-runner
   ./ns3 run "scratch/hpcc/third mix/config.txt"
   ./test.py --no-build -s hpcc

The example reads the topology, flow and trace-node files named in the
configuration. Relative paths are resolved from the simulation working
directory. In a flow file the first line specifies how many records to use.
Each record contains the source node, destination node, priority group,
destination port, payload size in bytes and start time in seconds.
The supplied ``mix/flow.txt`` selects its first two records.

To generate a configuration and run an algorithm with the upstream parameter
presets, use Python 3.

.. code-block:: bash

   python3 contrib/hpcc/run.py --cc hp --bw 100 --topo topology --trace flow
   python3 contrib/hpcc/run.py --cc dcqcn --bw 100 --topo topology --trace flow
   python3 contrib/hpcc/run.py --cc dctcp --bw 100 --topo topology --trace flow

``--bw`` specifies the NIC bandwidth in Gb/s used to derive parameters.
The topology file supplies the actual link rates. ``mix/config_doc.txt``
describes the configuration keys. ``CC_MODE`` selects DCQCN (1), HPCC (3),
TIMELY (7), DCTCP (8) or HPCC-PINT (10). For HPCC, the upstream INT wire
format holds five switch hops and encodes 25, 50, 100, 200 or 400 Gb/s.
QBB provides eight priority groups (0 through 7).

Outputs
-------

The FCT file contains one line per completed queue pair. Its fields are
source and destination IPv4 address (hex), source and destination port,
payload bytes, start time, flow completion time and estimated standalone
completion time. All three time fields are in nanoseconds. The PFC log records
the timestamp, node, node type, interface and pause (1) or resume (0) event.
Queue-length histograms go to ``QLEN_MON_FILE``.

With ``ENABLE_TRACE 1``, the binary packet trace begins with ``SimSetting``
and continues with ``TraceFormat`` records. It retains the upstream native
binary layout for use with the original ``analysis/trace_reader`` on a
matching architecture. The model accounts for 14 bytes of link-layer header.
HPCC-PINT accounts for the selected one- or two-byte telemetry width.

Integration and tests
---------------------

``QbbHelper`` installs devices and channels. The example builds the switch
and host forwarding tables from the topology, installs a driver on each host
and assigns addresses using IPv4. The priority-aware switch queue is
``BEgressQueue``. The separate ``LeakyBucket`` implements the upstream bounded
FIFO with packet- and byte-limit modes. Its data-rate and address attributes
carry configuration for a shaper. RDMA pacing is implemented in ``RdmaHw``.

``QbbRemoteChannel`` is built when ns-3 is configured with ``--enable-mpi``.
The helper selects it for links crossing MPI ranks when MPI is active.
The supplied scratch driver runs a single-process experiment.

The ``hpcc`` suite checks queue accounting, priority pause/resume, telemetry
width, counter rollover and two-sender incast. For each of HPCC, DCQCN and
DCTCP it checks both final acknowledgments, ECN echo, rate reduction and
PFC recovery. The HPCC case also checks the bottleneck rate and backlog
returned in an ACK.
