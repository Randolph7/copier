#include <openssl/bio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
// #include <sanitizer/asan_interface.h>

#include "config.h"

static const int server_port = 4433;
int msg_size = 0;

static volatile bool server_running = true;

#if defined(COPIER)

#include "copier.h"

int copier_fd = 0;
volatile long *bufferDescriptor;
volatile int *asyncRecv;
void **syncQueue;

#define NR_RECVFROM_COPYER 600
size_t arecv(long fd, void *__buf_base, size_t offset, size_t len, int flags)
{
	*bufferDescriptor = offset - 1;
	return syscall(NR_RECVFROM_COPYER, fd, __buf_base + offset, __buf_base, len, flags, (void *)bufferDescriptor);
}

#define QUEUE_TYPE_OUT 1
void createCpThread()
{
	copier_fd = syscall(602, -1, 2, QUEUE_TYPE_OUT);
	*syncQueue = mmap(NULL, sizeof(struct sync_queue), PROT_READ | PROT_WRITE, MAP_SHARED, copier_fd, 0);
}

int delCpThread(int queue_fd)
{
	return syscall(603, -1, queue_fd, QUEUE_TYPE_OUT);
}

int bindCpThread(int sock_fd)
{
	return syscall(605, sock_fd, copier_fd, QUEUE_TYPE_OUT);
}

void signalHandler(int signum)
{
	if (copier_fd)
		delCpThread(copier_fd);
	exit(signum);
}
#else
void createCpThread()
{
}
int bindCpThread(int sock_fd)
{
	return 0;
}
int delCpThread(int queue_fd)
{
	return 0;
}
void signalHandler(int signum)
{
	exit(signum);
}
#endif

static int create_socket(bool isServer)
{
	int s;
	int optval = 1;
	struct sockaddr_in addr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		perror("Unable to create socket");
		exit(EXIT_FAILURE);
	}

	if (isServer) {
		addr.sin_family = AF_INET;
		addr.sin_port = htons(server_port);
		addr.sin_addr.s_addr = INADDR_ANY;

		/* Reuse the address; good for quick restarts */
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
			perror("setsockopt(SO_REUSEADDR) failed");
			exit(EXIT_FAILURE);
		}

		if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("Unable to bind");
			exit(EXIT_FAILURE);
		}

		if (listen(s, 1) < 0) {
			perror("Unable to listen");
			exit(EXIT_FAILURE);
		}
	}

	return s;
}

int socket_fd;

int bio_read(BIO *b, char *buf, int len)
{
	// printf("recv len = %d buf=%lu\n", len, (unsigned long)buf);
#if defined(COPIER)
	if (len < 16384 && len < msg_size) {
		*asyncRecv = 0;
		return recv(socket_fd, buf, len, 0);
	} else {
		*asyncRecv = 1;
		// printf("arecv len = %d buf=%lu\n", len, (unsigned long)buf);
		return arecv(socket_fd, buf, 0, len, 0);
		// return recv(socket_fd, buf, len, 0);
	}
#else
	return recv(socket_fd, buf, len, 0);
#endif
}

int bio_write(BIO *b, const char *buf, int len)
{
	return send(socket_fd, buf, len, 0);
}

static long bio_ctrl(BIO *b, int cmd, long larg, void *pargs)
{
	long ret = 0;
	if (cmd == BIO_CTRL_PUSH)
		ret = 1;
	if (cmd == BIO_CTRL_POP)
		ret = 1;
	if (cmd == BIO_CTRL_FLUSH)
		ret = 1;
	if (cmd == BIO_C_SET_NBIO)
		ret = 1;
	(void)b, (void)cmd, (void)larg, (void)pargs;
	return ret;
}

static SSL_CTX *create_context()
{
	const SSL_METHOD *method;
	SSL_CTX *ctx;

	method = TLS_server_method();

	ctx = SSL_CTX_new(method);
	if (ctx == NULL) {
		perror("Unable to create SSL context");
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}

	return ctx;
}

static void configure_server_context(SSL_CTX *ctx)
{
	/* Set the key and cert */
	if (SSL_CTX_use_certificate_chain_file(ctx, "cert.pem") <= 0) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}

	if (SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM) <= 0) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
}

#define TIMES 10

int main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("./server length\n");
        exit(0);
    }
    msg_size = atoi(argv[1]);
    int times = atoi(argv[2]);

    printf("msg_size = %d\n", msg_size);

	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	int server_skt = -1;
	int client_skt = -1;

	unsigned long long total_time = 0;

	char* rxbuf = malloc(msg_size);
	memset(rxbuf, 0, msg_size);
	int rxlen;

	int recv_count = 0;
	struct sockaddr_in addr;
	unsigned int addr_len = sizeof(addr);

	struct timespec start, end;

	/* ignore SIGPIPE so that server can continue running when client pipe closes abruptly */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signalHandler);

#if defined(COPIER)
	bufferDescriptor = getBufferDescriptor();
	asyncRecv = getAsyncRecv();
	syncQueue = getSyncQueue();
#endif
	createCpThread();
	/* Create context used by both client and server */
	ssl_ctx = create_context();

	/* Configure server context with appropriate key files */
	configure_server_context(ssl_ctx);

	/* Create server socket; will bind with server port and listen */
	server_skt = create_socket(true);

	/*
    * Loop to accept clients.
    * Need to implement timeouts on TCP & SSL connect/read functions
    * before we can catch a CTRL-C and kill the server.
    */
	while (server_running) {
		total_time = 0;
		recv_count = 0;
		/* Wait for TCP connection from client */
		client_skt = accept(server_skt, (struct sockaddr *)&addr, &addr_len);
		socket_fd = client_skt;

		bindCpThread(client_skt);

		if (client_skt < 0) {
			perror("Unable to accept");
			exit(EXIT_FAILURE);
		}

		printf("Client TCP connection accepted\n");

		/* Create server SSL structure using newly accepted client socket */
		ssl = SSL_new(ssl_ctx);

		BIO_METHOD *meth = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "My Custom BIO");
		BIO_meth_set_read(meth, bio_read);
		BIO_meth_set_write(meth, bio_write);
		BIO_meth_set_ctrl(meth, bio_ctrl);
		BIO *custom_bio = BIO_new(meth);

		SSL_set_bio(ssl, custom_bio, custom_bio);

		/* Wait for SSL connection from the client */
		if (SSL_accept(ssl) <= 0) {
			ERR_print_errors_fp(stderr);
			server_running = false;
		} else {
			struct pollfd fds[1];
			fds[0].fd = client_skt;
			fds[0].events = POLLIN;

			int round = times;
			int min_recv = msg_size + 16;
			if(msg_size > 16384) {
				round = msg_size / 16384 * times;
				min_recv = 16384 + 16;
			}
			/* Echo loop */
			while (recv_count < round) {
				/* Get message from client; will fail if client closes connection */				
				recv_count++;

				int poll_ret = poll(fds, 1, 500);

				if (poll_ret > 0) {
					if (fds[0].revents & POLLIN) {
						int bytes_available = 0;
						while (bytes_available < min_recv) {
							if (ioctl(client_skt, FIONREAD, &bytes_available) >= 0) {
								// printf("Bytes available to read: %d\n", bytes_available);
							} else {
								perror("ioctl failed");
							}
						}
					}
				} else if (poll_ret == 0) {
					printf("Timeout occurred! No data after %d milliseconds.\n", 500);
				} else {
					perror("poll failed");
				}

				clock_gettime(CLOCK_MONOTONIC, &start);
				rxlen = SSL_read(ssl, rxbuf, msg_size);
				clock_gettime(CLOCK_MONOTONIC, &end);
				// printf("finish recv %d\n", recv_count);
				total_time += (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

				if (rxlen <= 0) {
					if (rxlen == 0) {
						printf("Client closed connection\n");
					} else {
						printf("SSL_read returned %d\n", rxlen);
					}
					ERR_print_errors_fp(stderr);
					break;
				}
				/* Insure null terminated input */
				rxbuf[rxlen] = 0;
				/* Echo it back */
				if (SSL_write(ssl, "OK", 3) <= 0) {
					ERR_print_errors_fp(stderr);
				}
			}
		}
		if (server_running) {
			/* Cleanup for next client */
			SSL_shutdown(ssl);
			SSL_free(ssl);
			close(client_skt);
		}
		printf("avg time = %llu\n", total_time / times);
	}
	/* Close up */
	if (ssl != NULL) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	SSL_CTX_free(ssl_ctx);

	if (client_skt != -1)
		close(client_skt);
	if (server_skt != -1)
		close(server_skt);

	printf("sslecho exiting\n");

	return EXIT_SUCCESS;
}
