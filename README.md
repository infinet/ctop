# Cluster top

For monitor cluster cpu, memory and network usage.

It invokes rsh to read /proc from each cluster node. The network interface is default to eth0.


## Usage

Tweak the THREADS macro in ctop.c to your need, default launches 8 threads to
rsh into computing nodes. Run `make` to build.

$ ./ctop

![Screenshot][]

The bar is green when the cpu or memory usage is below 50%; it becomes yellow
when usage rise above 50% but still below 70%. The bar turns into red when usage
is greater than 70%.

The example cluster has 48 nodes, a large window is needed to display all nodes
in one screen.

Type q to quit.


[Screenshot]: https://infinet.github.io/images/ctop.png
