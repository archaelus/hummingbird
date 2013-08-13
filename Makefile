CFLAGS=-Wall -g

all: hstress hserve

hstress: u.o hstress.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent

hserve: u.o hserve.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent

hplay: u.o hplay.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -levent

clean:
	rm -f hstress hserve hplay *.o

.PHONY: all clean
