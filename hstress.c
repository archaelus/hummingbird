/*
 * hstress - HTTP load generator with periodic output.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <event.h>
#include <evhttp.h>

#include "u.h"

#define NBUFFER 10
#define MAX_BUCKETS 100

#define OUTFILE_BUFFER_SIZE 4096
#define DRAIN_BUFFER_SIZE 4096

// cheap hack to get closer to our Hz target
#define USEC_FUDGE -300

char *http_hostname;
uint16_t http_port;
char http_hosthdr[2048];

struct{
	int count;
	int concurrency;
	int buckets[MAX_BUCKETS];
	int nbuckets;
	int rpc;
	int qps;

	int warmup;

	// for logging output time
	char *tsvout;
	FILE *tsvoutfile;

	// for logging reponse content
	char *responseout;
	FILE *responseoutfile;

	// request path
	char *path;
}params;

struct{
	int successes;
	int counters[MAX_BUCKETS + 1];
	int errors;
	int timeouts;
	int closes;
	int http_successes;
	int http_errors;
}counts;

int num_cols = 5;

struct request{
	struct timeval 			starttv;
	struct event			timeoutev;
	int 					sock;
	struct evhttp_connection *evcon;
	struct evhttp_request	*evreq;
	int					evcon_reqno;
};

struct Runner{
    struct timeval            tv;
    struct event              ev;
    struct evhttp_connection *evcon;
    struct request            *req;
    int                       reqno;
    int                       warmupRemaining;
};
typedef struct Runner Runner;

enum{
	Success,
	Closed,
	Error,
	Timeout
};

struct event 	reportev;
struct timeval 	reporttv ={ 1, 0 };
struct timeval	timeouttv ={ 1, 0 };
struct timeval 	lastreporttv;
int 			request_timeout;
struct timeval 	ratetv;
int 			ratecount = 0;
int			nreport = 0;
int 			nreportbuf[NBUFFER];
int 			*reportbuf[NBUFFER];

void recvcb(struct evhttp_request *req, void *arg);
void timeoutcb(int fd, short what, void *arg);
struct evhttp_connection *mkhttp();
void closecb(struct evhttp_connection *evcon, void *arg);
void report();
void sigint(int which);


unsigned char
rateLimitingEnabled()
{
    return params.qps > 0;
}

unsigned char
tsvOutputEnabled()
{
    return params.tsvout != nil;
}

unsigned char
responseRecordingEnabled()
{
    return params.responseout != nil;
}

/*
	Reporting.
*/

long
millisecondsSinceStart(struct timeval *tv)
{
    long milliseconds;
    struct timeval now, diff;

    gettimeofday(&now, nil);
    timersub(&now, tv, &diff);
    milliseconds = diff.tv_sec * 1000L + diff.tv_usec / 1000L;

    return milliseconds;
}

long
mkrate(struct timeval *tv, int count)
{
    long milliseconds;
    milliseconds = millisecondsSinceStart(tv);
	return(1000L * count / milliseconds);
}

void
resetTime(struct timeval *tv)
{
    gettimeofday(tv, nil);
}

void
reportcb(int fd, short what, void *arg)
{
	int i;

	printf("%d\t", nreport++);
	printf("%d\t", counts.errors);
	printf("%d\t", counts.timeouts);
	printf("%d\t", counts.closes);
  printf("%d\t", counts.http_successes);
  printf("%d\t", counts.http_errors);

	counts.errors = counts.timeouts = counts.closes = counts.http_successes = counts.http_errors = 0;

	for(i=0; params.buckets[i]!=0; i++)
		printf("%d\t", counts.counters[i]);

	printf("%d\n", counts.counters[i]);
	fflush(stdout);

	memset(counts.counters, 0, sizeof(counts.counters));

	if(params.count<0 || counts.successes<params.count)
		evtimer_add(&reportev, &reporttv);
}

/*
	HTTP, via libevent's HTTP support.
*/

struct evhttp_connection *
mkhttp()
{
	struct evhttp_connection *evcon;

	evcon = evhttp_connection_new(http_hostname, http_port);
	if(evcon == nil)
		panic("evhttp_connection_new");

	evhttp_connection_set_closecb(evcon, &closecb, nil);
	/*
		note: we manage our own per-request timeouts, since the underlying
		library does not give us enough error reporting fidelity
	*/

	/* also set some socket options manually. */


	return(evcon);
}

void
dispatch(Runner *runner, int reqno)
{
    struct evhttp_connection *evcon = runner->evcon;
	struct evhttp_request *evreq;
	struct request *req;

	if((req = calloc(1, sizeof(*req))) == nil)
		panic("calloc");

    runner->req = req;
	req->evcon = evcon;
	req->evcon_reqno = reqno;

	evreq = evhttp_request_new(&recvcb, runner);
	if(evreq == nil)
		panic("evhttp_request_new");

	req->evreq = evreq;

	evreq->response_code = -1;
	evhttp_add_header(evreq->output_headers, "Host", http_hosthdr);

	gettimeofday(&req->starttv, nil);
	evtimer_set(&req->timeoutev, timeoutcb,(void *)runner);
	evtimer_add(&req->timeoutev, &timeouttv);

	evhttp_make_request(evcon, evreq, EVHTTP_REQ_GET, params.path);
}



void
drainBuffer(struct request *req, FILE *outstream)
{
    struct evbuffer *requestBuffer;

    // are we re-allocating 4096bytes on each request? Seems inefficient.
    char charBuffer[DRAIN_BUFFER_SIZE];
    int bytesread = 0;

    requestBuffer = evhttp_request_get_input_buffer(req->evreq);

    do {
        bytesread = evbuffer_remove(requestBuffer, charBuffer, DRAIN_BUFFER_SIZE);
        fwrite(charBuffer, 1, bytesread, outstream);
    } while(bytesread > 0);
    fputs("---\n", outstream);
}

void
saveRequest(int how, Runner *runner)
{
    struct request *req = runner->req;
    int i;
    long startMicros, nowMicros;
	long milliseconds;
	struct timeval now, diff;


	if(runner->warmupRemaining > 0) {
	    runner->warmupRemaining--;
	    return;
	}

    gettimeofday(&now, nil);

    startMicros = req->starttv.tv_sec * 1000000 + req->starttv.tv_usec;
    nowMicros   = now.tv_sec * 1000000 + now.tv_usec;

    timersub(&now, &req->starttv, &diff);
    milliseconds = (nowMicros - startMicros)/1000;

    if(tsvOutputEnabled()) {
        fprintf(params.tsvoutfile, "%ld\t%ld\t%d\n", startMicros, nowMicros, how);
    }

    if(responseRecordingEnabled()) {
        drainBuffer(req, params.responseoutfile);
    }

    switch(how){
	case Success:
		for(i=0; params.buckets[i]<milliseconds &&
		    params.buckets[i]!=0; i++);
		counts.counters[i]++;
		counts.successes++;

		if (req->evreq->response_code == 200){
		  counts.http_successes++;
		}else{
		  counts.http_errors++;
		}
		break;
	case Error:
		counts.errors++;
		break;
	case Timeout:
		counts.timeouts++;
		break;
	}
}

void
complete(int how, Runner *runner)
{
    struct request *req = runner -> req;
	int total;
	saveRequest(how, runner);

	evtimer_del(&req->timeoutev);

	total =
	    counts.successes + counts.errors +
	    counts.timeouts /*+ counts.closes*/;
	/* enqueue the next one */
	if(params.count<0 || total<params.count){
	    // re-scheduling is handled by the callback
	    if(!rateLimitingEnabled()) {
            if(params.rpc<0 || params.rpc>req->evcon_reqno){
                dispatch(runner, req->evcon_reqno + 1);
            }else{
                evhttp_connection_free(req->evcon);
                runner->evcon = mkhttp();
                dispatch(runner, 1);
            }
        }
	}else{
		/* We'll count this as a close. I guess that's ok. */
		evhttp_connection_free(req->evcon);
		if(--params.concurrency == 0){
			evtimer_del(&reportev);
			reportcb(0, 0, nil);  /* issue a last report */
		}
	}


	free(req);
}

void
runnerEventCallback(int fd, short what, void *arg)
{
    Runner *runner = (Runner *)arg;
    if(rateLimitingEnabled()) {
        event_add(&runner->ev, &runner->tv);
    }

    dispatch(runner, ++ runner->reqno);
}

/**
 start a new, potentially, rate-limited runner
 */
void
startNewRunner()
{
    Runner *runner = calloc(1, sizeof(Runner));
    if(runner == nil)
        panic("calloc");

    runner->evcon = mkhttp();
    runner->warmupRemaining = params.warmup;

    if(rateLimitingEnabled()) {
        runner->tv.tv_sec = 0;
        runner->tv.tv_usec = 1000000/params.qps + USEC_FUDGE;

        if(runner->tv.tv_usec < 1)
            runner->tv.tv_usec = 1;

        evtimer_set(&runner->ev, runnerEventCallback, runner);
        evtimer_add(&runner->ev, &runner->tv);
    } else {
        // skip the timers and just loop as fast as possible
        runnerEventCallback(0, 0, runner);
    }
}

void
recvcb(struct evhttp_request *evreq, void *arg)
{

	int status = Success;

	/*
		It seems that, under certain circumstances,
		evreq may be null on failure.

		we'll count it as an error.
	*/

	if(evreq == nil || evreq->response_code < 0)
		status = Error;

	complete(status, (Runner *)arg);
}

void
timeoutcb(int fd, short what, void *arg)
{
    Runner *runner = (Runner *)arg;
	struct request *req = runner->req;

	/* re-establish the connection */
	evhttp_connection_free(req->evcon);
	req->evcon = mkhttp();

	complete(Timeout, runner);
}

void
closecb(struct evhttp_connection *evcon, void *arg)
{
	counts.closes++;
}


/*
	Aggregation.
*/

void
chldreadcb(struct bufferevent *b, void *arg)
{
	char *line, *sp, *ap;
	int n, i, total, nprocs = *(int *)arg;

	if((line=evbuffer_readline(b->input)) != nil){
		sp = line;

		if((ap = strsep(&sp, "\t")) == nil)
			panic("report error\n");
		n = atoi(ap);
		if(n - nreport > NBUFFER)
			panic("a process fell too far behind\n");

		n %= NBUFFER;

		for(i=0; i<params.nbuckets + num_cols && (ap=strsep(&sp, "\t")) != nil; i++)
			reportbuf[n][i] += atoi(ap);

		if(++nreportbuf[n] >= nprocs){
			/* Timestamp it.  */
			printf("%d\t",(int)time(nil));
			for(i = 0; i < params.nbuckets + num_cols; i++)
				printf("%d\t", reportbuf[n][i]);

			/* Compute the total rate of succesful requests. */
			total = 0;
			for(i=num_cols; i<params.nbuckets+num_cols; i++)
				total += reportbuf[n][i];

			printf("%ld\n", mkrate(&lastreporttv, total));
			resetTime(&lastreporttv);

			/* Aggregate. */
			counts.errors += reportbuf[n][0];
			counts.timeouts += reportbuf[n][1];
			counts.closes += reportbuf[n][2];
			counts.http_successes += reportbuf[n][3];
			counts.http_errors += reportbuf[n][4];
			for(i=0; i<params.nbuckets; i++){
				counts.successes += reportbuf[n][i + num_cols];
				counts.counters[i] += reportbuf[n][i + num_cols];
			}

			/* Clear it. Advance nreport. */
			memset(reportbuf[n], 0,(params.nbuckets + num_cols) * sizeof(int));
			nreportbuf[n] = 0;
			nreport++;
		}

		free(line);
	}

	bufferevent_enable(b, EV_READ);
}

void
chlderrcb(struct bufferevent *b, short what, void *arg)
{
	bufferevent_setcb(b, nil, nil, nil, nil);
	bufferevent_disable(b, EV_READ | EV_WRITE);
	bufferevent_free(b);

	/*if(--(*nprocs) == 0)
		event_loopbreak();*/
}

void
parentd(int nprocs, int *sockets)
{
	int *fdp, i, status;
	pid_t pid;
	struct bufferevent *b;

	signal(SIGINT, sigint);

	gettimeofday(&ratetv, nil);
	gettimeofday(&lastreporttv, nil);
	memset(nreportbuf, 0, sizeof(nreportbuf));
	for(i=0; i<NBUFFER; i++){
		if((reportbuf[i] = calloc(params.nbuckets + num_cols, sizeof(int))) == nil)
			panic("calloc");
	}

	event_init();

	for(fdp=sockets; *fdp!=-1; fdp++){
		b = bufferevent_new(
		    *fdp, chldreadcb, nil,
		    chlderrcb,(void *)&nprocs);
		bufferevent_enable(b, EV_READ);
	}

	event_dispatch();

	for(i=0; i<nprocs; i++)
		pid = waitpid(0, &status, 0);

	report();
}

void
sigint(int which)
{
	report();
	exit(0);
}

void
printcount(const char *name, int total, int count)
{
	fprintf(stderr, "# %s", name);
	if(total > 0)
		fprintf(stderr, "\t%d\t%.05f", count,(1.0f*count) /(1.0f*total));

	fprintf(stderr, "\n");
}

void
report()
{
	char buf[128];
	int i, total = counts.successes + counts.errors + counts.timeouts;

	printcount("successes", total, counts.successes);
	printcount("errors", total, counts.errors);
	printcount("timeouts", total, counts.timeouts);
	printcount("closes", total, counts.closes);
	printcount("200s   ", total, counts.http_successes);
	printcount("!200s  ", total, counts.http_errors);
	for(i=0; params.buckets[i]!=0; i++){
		snprintf(buf, sizeof(buf), "<%d\t", params.buckets[i]);
		printcount(buf, total, counts.counters[i]);
	}

	snprintf(buf, sizeof(buf), ">=%d\t", params.buckets[i - 1]);
	printcount(buf, total, counts.counters[i]);

	/* no total */
	fprintf(stderr, "# time\t\t%.3f\n", millisecondsSinceStart(&ratetv)/1000.0);
	fprintf(stderr, "# hz\t\t%ld\n", mkrate(&ratetv, counts.successes));
}

/*
	Main, dispatch.
*/

void
usage(char *cmd)
{
	fprintf(
		stderr,
		"%s: [-c CONCURRENCY] [-b BUCKETS]\n"
		"[-n COUNT] [-p NUMPROCS] [-r INTERVAL] [-u GET_PATH]\n"
		"[-o TIMING_LOG] [-x RESPONSE_LOG]\n"
		"[HOST] [PORT]\n",
		cmd);

	exit(0);
}

int
main(int argc, char **argv)
{
	int ch, i, nprocs = 1, is_parent = 1, port, *sockets, fds[2];
	pid_t pid;
	char *sp, *ap, *host, *cmd = argv[0];

	/* Defaults */
	params.count = -1;
	params.rpc = -1;
	params.concurrency = 1;
	memset(params.buckets, 0, sizeof(params.buckets));
	params.buckets[0] = 1;
	params.buckets[1] = 10;
	params.buckets[2] = 100;
	params.nbuckets = 4;
	params.path = "/";

	memset(&counts, 0, sizeof(counts));

	while((ch = getopt(argc, argv, "c:l:b:n:p:r:i:u:o:w:x:h")) != -1){
		switch(ch){
		case 'b':
			sp = optarg;

			memset(params.buckets, 0, sizeof(params.buckets));

			for(i=0; i<MAX_BUCKETS && (ap=strsep(&sp, ",")) != nil; i++)
				params.buckets[i] = atoi(ap);

			params.nbuckets = i+1;

			if(params.buckets[0] == 0)
				panic("first bucket must be >0\n");

			for(i=1; params.buckets[i]!=0; i++){
				if(params.buckets[i]<params.buckets[i-1])
					panic("invalid bucket specification!\n");
			}
			break;

		case 'c':
			params.concurrency = atoi(optarg);
			break;

		case 'n':
			params.count = atoi(optarg);
			break;

		case 'p':
			nprocs = atoi(optarg);
			break;

		case 'i':
			reporttv.tv_sec = atoi(optarg);
			break;

		case 'r':
			params.rpc = atoi(optarg);
			break;

	    case 'l':
	        params.qps = atoi(optarg);
	        break;

        case 'w':
            params.warmup = atoi(optarg);
            break;

	    case 'o':
	        params.tsvout = optarg;
	        params.tsvoutfile = fopen(params.tsvout, "w+");

	        if(params.tsvoutfile == nil)
	            panic("Could not open TSV outputfile: %s", optarg);
	        break;

	    case 'x':
	        params.responseout = optarg;
	        params.responseoutfile = fopen(params.responseout, "w+");
	        if(params.responseoutfile == nil)
	            panic("Could not open response record file: %s", optarg);
	        break;

	    case 'u':
	        params.path = optarg;
	        break;

		case 'h':
			usage(cmd);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	host = "127.0.0.1";
	port = 80;
	switch(argc){
	case 2:
		port = atoi(argv[1]);
	case 1:
		host = argv[0];
	case 0:
		break;
	default:
		panic("only 0 or 1(host port) pair are allowed\n");
	}

	http_hostname = host;
	http_port = port;
	if(snprintf(http_hosthdr, sizeof(http_hosthdr), "%s:%d", host, port) > sizeof(http_hosthdr))
		panic("snprintf");

	for(i = 0; params.buckets[i] != 0; i++)
		request_timeout = params.buckets[i];

	if(params.count > 0)
		params.count /= nprocs;

	fprintf(stderr, "# params: %s:%d c=%d p=%d n=%d r=%d l=%d u=%s\n",
	    http_hostname, http_port, params.concurrency, nprocs, params.count, params.rpc, params.qps, params.path);

	fprintf(stderr, "# ts\t\terrors\ttimeout\tcloses\t200s\t!200s\t");
	for(i=0; params.buckets[i]!=0; i++)
		fprintf(stderr, "<%d\t", params.buckets[i]);

	fprintf(stderr, ">=%d\thz\n", params.buckets[i - 1]);

	if((sockets = calloc(nprocs + 1, sizeof(int))) == nil)
		panic("malloc\n");

	sockets[nprocs] = -1;

	for(i=0; i<nprocs; i++){
		if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
			perror("socketpair");
			exit(1);
		}

		sockets[i] = fds[0];

		if((pid = fork()) < 0){
			kill(0, SIGINT);
			perror("fork");
			exit(1);
		}else if(pid != 0){
			close(fds[1]);
			continue;
		}

		is_parent = 0;

		event_init();

		/* Set up output. */
		if(dup2(fds[1], STDOUT_FILENO) < 0){
			perror("dup2");
			exit(1);
		}

		close(fds[1]);

		// create a buffer for this process
		if(tsvOutputEnabled()) {
            char *outBuffer = mal(sizeof(char) * OUTFILE_BUFFER_SIZE);
            setvbuf(params.tsvoutfile, outBuffer, _IOLBF, OUTFILE_BUFFER_SIZE);
		}

		if(responseRecordingEnabled()) {
		    char *outBuffer = mal(sizeof(char) * OUTFILE_BUFFER_SIZE);
		    setvbuf(params.responseoutfile, outBuffer, _IOLBF, OUTFILE_BUFFER_SIZE);
		}

		for(i=0; i<params.concurrency; i++)
		    startNewRunner();

        /* event handler for reports */
		evtimer_set(&reportev, reportcb, nil);
		evtimer_add(&reportev, &reporttv);

		event_dispatch();

		break;
	}

	if(is_parent)
		parentd(nprocs, sockets);

	return(0);
}
