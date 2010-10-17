.PHONY: clean

CFLAGS=-Wall -O3 -g

avrprogni:	avrprogni.c
	$(CC) $(CFLAGS) -o avrprogni -lm -lcomedi $<

clean:
	rm -f avrprogni
