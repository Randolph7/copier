#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "config.h"

static const int server_port = 4433;
int msg_size = 0;

typedef unsigned char bool;
#define true 1
#define false 0

/*
 * This flag won't be useful until both accept/read (TCP & SSL) methods
 * can be called with a timeout. TBD.
 */
static volatile bool server_running = true;

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

static SSL_CTX *create_context()
{
	const SSL_METHOD *method;
	SSL_CTX *ctx;

	method = TLS_client_method();

	ctx = SSL_CTX_new(method);
	if (ctx == NULL) {
		perror("Unable to create SSL context");
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}

	return ctx;
}

static void configure_client_context(SSL_CTX *ctx)
{
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("./server length\n");
        exit(0);
    }
    msg_size = atoi(argv[1]);
    printf("msg_size = %d\n", msg_size);

	int result;

	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	int server_skt = -1;
	int client_skt = -1;

	/* used by fgets */
	char* buffer = malloc(msg_size);

	char rxbuf[3];
	int rxlen;

	char *rem_server_ip = NULL;

	struct sockaddr_in addr;

	/* ignore SIGPIPE so that server can continue running when client pipe closes abruptly */
	signal(SIGPIPE, SIG_IGN);

	rem_server_ip = "127.0.0.1";

	/* Create context used by both client and server */
	ssl_ctx = create_context();

	/* Configure client context so we verify the server correctly */
	configure_client_context(ssl_ctx);

	/* Create "bare" socket */
	client_skt = create_socket(false);
	/* Set up connect address */
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, rem_server_ip, &addr.sin_addr.s_addr);
	addr.sin_port = htons(server_port);
	/* Do TCP connect with server */
	if (connect(client_skt, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("Unable to TCP connect to server");
		goto exit;
	} else {
		printf("TCP connection to server successful\n");
	}

	/* Create client SSL structure using dedicated client socket */
	ssl = SSL_new(ssl_ctx);
	if (!SSL_set_fd(ssl, client_skt)) {
		ERR_print_errors_fp(stderr);
		goto exit;
	}
	/* Set hostname for SNI */
	SSL_set_tlsext_host_name(ssl, rem_server_ip);
	/* Configure server hostname check */
	if (!SSL_set1_host(ssl, rem_server_ip)) {
		ERR_print_errors_fp(stderr);
		goto exit;
	}

	/* Now do SSL connect with server */
	if (SSL_connect(ssl) == 1) {
		memset(buffer, '1', msg_size);
		buffer[msg_size - 1] = '\0';

		printf("SSL connection to server successful\n\n");

		/* Loop to send input from keyboard */
		while (true) {
			/* Send it to the server */
			if ((result = SSL_write(ssl, buffer, msg_size)) <= 0) {
				printf("Server closed connection\n");
				ERR_print_errors_fp(stderr);
				break;
			}

			/* Wait for the echo */
			rxlen = SSL_read(ssl, rxbuf, 3);
			if (rxlen <= 0) {
				printf("Server closed connection\n");
				ERR_print_errors_fp(stderr);
				break;
			}
		}
	} else {
		printf("SSL connection to server failed\n\n");

		ERR_print_errors_fp(stderr);
	}

exit:
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
