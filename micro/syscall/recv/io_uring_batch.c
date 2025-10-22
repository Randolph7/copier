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

#define QUEUE_DEPTH 100

volatile int send_finish = 0;
volatile int can_stop = 0;
volatile long total_msg_len = 0;
volatile int server_established = 0;
volatile int port = 0;
int msg_read_len;
int recv_buf_size = SOCK_BUF_LEN;

void *server(void *arg)
{
	int opt = 1;
	struct timespec start, end;
	long long int nanoseconds;
	char buffer[MAX_READ_LEN];
	long total_msg_len_copy;
	ssize_t bytes_read;
	long msg_read_count = 0;
	int ret;

	struct io_uring ring;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;

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

	while (!send_finish)
		;

	total_msg_len_copy = total_msg_len;

	ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
	if (ret < 0) {
		fprintf(stderr, "io_uring_queue_init failed\n");
		return NULL;
	}

	int ud = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (msg_read_count < 300 & total_msg_len > msg_read_len) {
		// bytes_read = recv(client_sock, buffer, msg_read_len, 0);
		msg_read_count++;
		sqe = io_uring_get_sqe(&ring);
		if (!sqe) {
			fprintf(stderr, "io_uring_get_sqe failed\n");
			return NULL;
		}
		io_uring_prep_recv(sqe, client_sock, buffer, msg_read_len, 0);
		sqe->user_data = ud;
		ud++;

		if (ud == QUEUE_DEPTH) {
			ret = io_uring_submit(&ring);
			if (ret < 0) {
				fprintf(stderr, "io_uring_submit failed\n");
				return NULL;
			}

			for (int i = 0; i < ud; i++) {
				ret = io_uring_wait_cqe(&ring, &cqe);
				if (ret < 0) {
					fprintf(stderr, "io_uring_wait_cqe failed\n");
					return NULL;
				}

				if (cqe->res < 0) {
					fprintf(stderr, "Async recv failed: %s\n", strerror(-cqe->res));
					io_uring_cqe_seen(&ring, cqe);
					return NULL;
				}
				io_uring_cqe_seen(&ring, cqe);
			}
			ud = 0;
		}

		total_msg_len -= msg_read_len;
	}
	ret = io_uring_submit(&ring);
	if (ret < 0) {
		fprintf(stderr, "io_uring_submit failed\n");
		return NULL;
	}

	for (int i = 0; i < ud; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "io_uring_wait_cqe failed\n");
			return NULL;
		}

		if (cqe->res < 0) {
			fprintf(stderr, "Async recv failed: %s\n", strerror(-cqe->res));
			io_uring_cqe_seen(&ring, cqe);
			return NULL;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	io_uring_queue_exit(&ring);

	// msg_read_count = (total_msg_len_copy - total_msg_len) / msg_read_len;
	nanoseconds = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

	printf("write %ld K data, read %d for %ld times, avg latency = %lld\n", total_msg_len_copy / 1024, msg_read_len, msg_read_count,
	       nanoseconds / msg_read_count);

	can_stop = 1;
	close(client_sock);
	close(server_fd);
	return NULL;
}

void *client(void *arg)
{
	int sock = 0;
	long msg_len;
	struct sockaddr_in serv_addr;
	char *msg = malloc(MSG_LENGTH);
	memset(msg, 1, MSG_LENGTH);

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

	while ((msg_len = send(sock, msg, MSG_LENGTH, 0)) > 0)
		total_msg_len += msg_len;
	send_finish = 1;

	while (!can_stop)
		;
	return NULL;
}

int main(int argc, char *argv[])
{
	msg_read_len = atoi(argv[1]);
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
