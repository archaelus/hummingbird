hummingbird - no bullshit HTTP load testing suite

# hstress

`hstress` generates large HTTP workloads.

Options are as follows:

    hstress [-c CONCURRENCY] [-b BUCKETS] [-n COUNT] [-p NUMPROCS]
    [-r RPC] [-i INTERVAL] [-o TSV RECORD] [-l MAX_QPS] [-w WARMUP]
    [-u PATH] [HOST] [PORT]

The default host is `127.0.0.1`, and the default port is `80`.

* `-c` controls concurrency. This is the number of outstanding
  requests at a given time

* `-b` sets the "bucket spec".  This determines how we bucket the
  measurement histograms. Set to a comma-separated list of values in
  milliseconds. For example `1,10,100,250,500` will bucket requests
  into the given amount of milliseconds.

* `-n` controls the total number of requests to make. Left unspecified,
  `hb` never terminates. The total may be exceeded by up to (concurrency - 1) requests.

* `-p` controls the number of processes to fork (for multiple event
  loops). The default value is `1`.

* `-r` specifies the number of requests per connection, e.g. keep-alive (default is no limit)

* `-i` specifies the reporting interval in seconds

* `-o` output each request's stats to a TSV-formatted file

* `-l` limits the request rate on each concurrent thread on each process (in hertz; defaults to no limit)

* `-w` specifies a warmup for each thread (the number of ignored requests)

* `-u` allows specifying a path other than `/`.

`hstress` produces output like the following:

    # params: -c 50 -n -1 -p 1 -r 0 -i 1 -l 0 -u / localhost 80
    #               conn    conn    conn    conn    http    http
    # ts            success errors  timeout closes  success error   <1      <5      <10     <50     <100    <500    <1000   >=1000  hz
    1332095312      678     0       0       678     678     0       0       0       0       214     297     167     0       0       642
    1332095313      624     0       0       624     624     0       0       0       2       219     229     173     1       0       626
    1332095314      677     0       1       678     677     0       0       0       24      405     112     136     0       0       671
    1332095315      702     0       13      715     702     0       0       0       1       446     179     76      0       0       657
    1332095316      517     0       19      536     517     0       0       0       20      240     160     97      0       0       523
    1332095317      422     0       23      445     422     0       0       0       19      125     136     142     0       0       420
    1332095318      652     0       10      662     652     0       0       0       3       255     311     83      0       0       639
    1332095319      648     0       0       648     648     0       0       0       7       356     182     103     0       0       637
    1332095320      640     0       16      656     640     0       0       0       2       300     192     146     0       0       640
    # conn_successes        5560    0.98547
    # conn_errors           0       0.00000
    # conn_timeouts         82      0.01453
    # conn_closes           5642    1.00000
    # http_successes        5560    0.98547
    # http_errors           0       0.00000
    # <1                    0       0.00000
    # <5                    0       0.00000
    # <10                   78      0.01382
    # <50                   2560    0.45374
    # <100                  1798    0.31868
    # <500                  1123    0.19904
    # <1000                 1       0.00018
    # >=1000                0       0.00000
    # time                  10.027
    # hz                    562

The first column is the timestamp, and the subsequent columns are
according to the specified bucketing (controlled via `-b`). Only
successful requests (HTTP 200) are counted in the histogram.

This output format is handy for analysis with the standard Unix tools.
The banner is written to `stderr`, so only the data values are emitted
to `stdout`.

Data is written to TSV in the format start microseconds, stop microseconds, status (0 for Success):

    1322596079103530    1322596079103953    0
    1322596079103673    1322596079104079    0
    1322596079103967    1322596079104411    0
    1322596079103771    1322596079104419    0
    1322596079104092    1322596079104566    0
    1322596079104425    1322596079104772    0
    1322596079104433    1322596079104804    0
    1322596079104578    1322596079105082    0
    1322596079104786    1322596079105219    0
    1322596079104818    1322596079105387    0

# hplay

`hplay` replays http requests at a constant rate. Eg.

    # hplay localhost 8000 100 httpreqs

will replay the HTTP requests stored in `httpreqs` to `localhost:8000` at a rate of 100 per second. Request parsing is robust so you can give it packet dumps.

For example, on a server host that receives requests you wish to replay:

    $ tcpdump -n -c500 -i any dst port 10100 -s0 -w capture

Then reconstruct it with [tcpflow](http://www.circlemud.org/~jelson/software/tcpflow/):

    $ tcpflow -r capture -c | sed 's/^...\....\....\....\......\-...\....\....\....\......: //g' > reqs

And finally, replay these requests onto localhost:8080:

    $ hplay localhost 8000 100 reqs

# hserve

`hserve` is a simple HTTP server that will yield a constant response.
