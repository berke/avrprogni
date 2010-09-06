CFLAGS=-Wall -O3 -g

paravr:	parallel.c
	$(CC) $(CFLAGS) -o paravr -lm -lcomedi $<

clean:
	rm -f paravr
