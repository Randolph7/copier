#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <liburing.h>

#define MSG_LENGTH (16 * 1024)
#define MAX_READ_LEN (512 * 1024)
#define SOCK_BUF_LEN (256 * 1024 * 1024)
#define BACKLOG 1
#define ip "127.0.0.1"
#define SERVER_CORE 0
#define CLIENT_CORE 4

#define QUEUE_DEPTH 4096

#define TIME 1

volatile int send_finish = 0;
volatile int can_stop = 0;
volatile long total_msg_len = 0;
volatile int server_established = 0;
volatile int port = 0;
int msg_send_len;
int send_times;
int recv_buf_size = SOCK_BUF_LEN;

const int msg_read_len = (1024 * 16);

void *server(void *arg)
{
	int opt = 1;
	char buffer[MAX_READ_LEN];

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(SERVER_CORE, &cpuset);

	pthread_t current_thread = pthread_self();
	if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
		perror("pthread_setaffinity_np");
	}

	int server_fd, client_sock;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	socklen_t len = sizeof(address);
	getsockname(server_fd, (struct sockaddr *)&address, &len);
	port = ntohs(address.sin_port);
	if (listen(server_fd, 10) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	client_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

	if (setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size)) < 0) {
		perror("setsockopt error");
		exit(EXIT_FAILURE);
	}

	// fcntl(client_sock, F_SETFL, O_NONBLOCK);

	while (recv(client_sock, buffer, msg_send_len, 0) > 0)
			;

	// for (int i = 0; i < TIME; i++) {
	// 	while (!send_finish)
	// 		;

	// 	while (recv(client_sock, buffer, msg_send_len, 0) > 0)
	// 		;
	// 	send_finish = 0;
	// }

	close(client_sock);
	close(server_fd);
	return NULL;
}

void *client(void *arg)
{
	int sock = 0;
	long msg_len;
	struct sockaddr_in serv_addr;
	struct timespec start, end;
	long long int nanoseconds;
	long msg_send_count;

	struct io_uring ring;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(CLIENT_CORE, &cpuset);

	pthread_t current_thread = pthread_self();
	if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
		perror("pthread_setaffinity_np");
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);

	while (!port)
		;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
		perror("Invalid address / Address not supported");
		return NULL;
	}
	while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		;
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &recv_buf_size, sizeof(recv_buf_size)) < 0) {
		perror("setsockopt error");
		exit(EXIT_FAILURE);
	}
	fcntl(sock, F_SETFL, O_NONBLOCK);

	int ret = io_uring_queue_init(4096, &ring, 0);
	if (ret < 0) {
		fprintf(stderr, "io_uring_queue_init failed, err = %d\n", ret);
		return NULL;
	}

	for (int m = 0; m < TIME; m++) {
		total_msg_len = 0;
		char *msg = malloc(MSG_LENGTH * 1024);
		memset(msg, 1, MSG_LENGTH * 1024);
		clock_gettime(CLOCK_MONOTONIC, &start);
		int t;
		for (t = 0; t < send_times; t++) {
			// msg_len = send(sock, msg + total_msg_len, msg_send_len, 0);
			sqe = io_uring_get_sqe(&ring);
			if (!sqe) {
				fprintf(stderr, "io_uring_get_sqe failed\n");
				return NULL;
			}
			io_uring_prep_send(sqe, sock, msg + total_msg_len, msg_send_len, 0);
			sqe->user_data = t;
			total_msg_len += msg_send_len;

			if ((t + 1) % QUEUE_DEPTH == 0) {
				ret = io_uring_submit(&ring);
				if (ret < 0) {
					fprintf(stderr, "io_uring_submit failed\n");
					return NULL;
				}
				for (int i = 0; i < QUEUE_DEPTH; i++) {
					ret = io_uring_wait_cqe(&ring, &cqe);
					if (ret < 0) {
						printf("only send %d times\n", t);
						return NULL;
					}

					if (cqe->res < 0) {
						fprintf(stderr, "async send failed: %s\n", strerror(-cqe->res));
						io_uring_cqe_seen(&ring, cqe);
						return NULL;
					}
					io_uring_cqe_seen(&ring, cqe);
				}
			}
		}
		ret = io_uring_submit(&ring);
		if (ret < 0) {
			fprintf(stderr, "io_uring_submit failed\n");
			return NULL;
		}
		for (int i = 0; i < t % QUEUE_DEPTH; i++) {
			ret = io_uring_wait_cqe(&ring, &cqe);
			if (ret < 0) {
				printf("only send %d times\n", t);
				return NULL;
			}

			if (cqe->res < 0) {
				fprintf(stderr, "async send failed: %s\n", strerror(-cqe->res));
				io_uring_cqe_seen(&ring, cqe);
				return NULL;
			}
			io_uring_cqe_seen(&ring, cqe);
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		send_finish = 1;
		// while (send_finish)
		// 	;
		close(sock);
		free(msg);
	}

	msg_send_count = send_times;
	nanoseconds = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

	printf("write %ld K data, send %d for %ld times, avg latency = %lld\n", total_msg_len / 1024, msg_send_len, msg_send_count,
	       nanoseconds / msg_send_count);

	return NULL;
}

int main(int argc, char *argv[])
{
	msg_send_len = atoi(argv[1]);
	send_times = atoi(argv[2]);
	pthread_t client_t, server_t;

	if (pthread_create(&client_t, NULL, client, NULL) != 0) {
		perror("pthread_create client");
		exit(EXIT_FAILURE);
	}
	if (pthread_create(&server_t, NULL, server, NULL) != 0) {
		perror("pthread_create server");
		exit(EXIT_FAILURE);
	}
	pthread_join(client_t, NULL);
	pthread_join(server_t, NULL);
	return 0;
}
