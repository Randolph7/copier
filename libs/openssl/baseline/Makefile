CFLAGS  = -I../openssl-ins/include/openssl -g -Wall
LDLIBS  = -L../openssl-ins/lib64 -lssl -lcrypto

all: server client

server:
	$(CC) $(CFLAGS) $(LDFLAGS) -o server server.c $(LDLIBS)

client:
	$(CC) $(CFLAGS) $(LDFLAGS) -o client client.c $(LDLIBS)

clean:
	$(RM) client server *.o

test: all
	@echo "\nSSL Echo tests:"
	@echo "skipped"
