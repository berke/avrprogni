CFLAGS=-Wall -O2 -static -g

paravr:	parallel.c
	$(CC) $(CFLAGS) -o paravr $<

clean:
	rm -f paravr
