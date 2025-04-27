CC = clang

all: tcp_client tcp_server udp_client udp_server 

tcp_client: tcp_client.c
	$(CC) $(CFLAGS) -o $@ $<

tcp_server: tcp_server.c
	$(CC) $(CFLAGS) -o $@ $<

udp_client: udp_client.c
	$(CC) $(CFLAGS) -o $@ $<

udp_server: udp_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f tcp_client tcp_server udp_client udp_server

.PHONY: all clean
