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

  # params: localhost:80 c=100 p=1 n=5000 r=-1 l=0 u=/
  #             conn    conn    conn    conn    http    http
  # ts          success errors  timeout closes  success error   <1      <10     <100    <250    <500    >=500   hz
  1332055454    638     0       1       0       638     0       0       0       2       587     48      1       592
  1332055455    823     0       0       0       823     0       0       0       6       814     0       3       823
  1332055456    809     0       0       0       809     0       0       0       3       806     0       0       809
  1332055457    788     0       0       0       788     0       0       0       1       787     0       0       788
  1332055458    802     0       0       0       802     0       0       0       5       797     0       0       802
  1332055459    797     0       0       0       797     0       0       0       1       795     0       1       797
  1332055460    442     0       0       100     442     0       0       0       0       442     0       0       760
  # conn_successes      5099    0.99980
  # conn_errors         0       0.00000
  # conn_timeouts       1       0.00020
  # conn_closes         100     0.01961
  # http_successes      5099    0.99980
  # http_errors         0       0.00000
  # <1                        0 0.00000
  # <10                 0       0.00000
  # <100                        18      0.00353
  # <250                        5028    0.98588
  # <500                        48      0.00941
  # >=500                       5       0.00098
  # time                        6.659
  # hz                  765

The first column is the timestamp, and the subsequent columns are
according to the specified bucketing (controlled via `-b`). Only
successful requests (HTTP 200) are counted in the histogram.

This output format is handy for analysis with the standard Unix tools.
The banner is written to `stderr`, so only the data values are emitted
to `stdout`.

Data is written to TSV in the format start microseconds, stop microseconds, status (0 for Success!)

  1322596079103530        1322596079103953        0
  1322596079103673        1322596079104079        0
  1322596079103967        1322596079104411        0
  1322596079103771        1322596079104419        0
  1322596079104092        1322596079104566        0
  1322596079104425        1322596079104772        0
  1322596079104433        1322596079104804        0
  1322596079104578        1322596079105082        0
  1322596079104786        1322596079105219        0
  1322596079104818        1322596079105387        0

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
