CFLAGS += -DPOSIX -O2 -Wall -Werror -Wpedantic -Wextra -Wswitch-default -Wformat=2

all: jperf.c
	$(CC) $(CFLAGS) jperf.c -o jperf

clean:
	rm jperf
