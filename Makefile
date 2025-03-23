all:
	clang server.c -o server -pthread -lrt
	clang client.c -o client -pthread -lrt

clean:
	rm -f server client
