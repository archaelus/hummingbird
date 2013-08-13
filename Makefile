CFLAGS=-Wall -g

all: hstress hserve

hstress: u.o hstress.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^

hserve: u.o hserve.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^

hplay: u.o hplay.o
	$(CC) $(CFLAGS) $(LDFLAGS) -levent -o $@ $^

clean:
	rm -f hstress hserve hplay *.o

.PHONY: all clean
