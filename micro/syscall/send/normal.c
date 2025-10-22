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

#define MSG_LENGTH (16 * 1024)
#define MAX_READ_LEN (512 * 1024)
#define SOCK_BUF_LEN (256 * 1024 * 1024)
#define BACKLOG 1
#define ip "127.0.0.1"
#define SERVER_CORE 0
#define CLIENT_CORE 4

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

	char ok[] = "OK";
	size_t recv_len = 0;
	for (int i = 0; i < send_times; i++) {
		recv_len = 0;
		while (recv_len != msg_send_len) {
			recv_len += recv(client_sock, buffer, msg_send_len, 0);
		}
		send(client_sock, ok, strlen(ok), 0);
	}

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
	long long int nanoseconds = 0;
	long msg_send_count;

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

	for (int i = 0; i < TIME; i++) {
		char recv_buf[100];
		total_msg_len = 0;
		char *msg = malloc(MSG_LENGTH * 4096);
		memset(msg, 1, MSG_LENGTH * 4096);
		for (int t = 0; t < send_times; t++) {
			// printf("%d\n", t);
			// msg_len = send(sock, msg + total_msg_len, msg_send_len, 0);
			clock_gettime(CLOCK_MONOTONIC, &start);
			msg_len = send(sock, msg + total_msg_len, msg_send_len, 0);
			if (msg_len < 0) {
				// printf("only send %d times\n", t);
				break;
			}
			clock_gettime(CLOCK_MONOTONIC, &end);
			nanoseconds += (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
			recv(sock, recv_buf, 100, 0);
			total_msg_len += msg_len;
		}
		send_finish = 1;
		free(msg);
	}

	msg_send_count = total_msg_len / msg_send_len;

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
