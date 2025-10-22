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

#define TIME 300

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
	long msg_read_count;

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

	for (int i = 0; i < TIME; i++) {
		while (!send_finish)
			;

		total_msg_len_copy = total_msg_len;
		clock_gettime(CLOCK_MONOTONIC, &start);
		while (total_msg_len > msg_read_len) {
			bytes_read = recv(client_sock, buffer, msg_read_len, 0);
			// if(bytes_read != msg_read_len)
			//     perror("read broken");
			total_msg_len -= msg_read_len;
		}
		clock_gettime(CLOCK_MONOTONIC, &end);
		send_finish = 0;
	}

	msg_read_count = (total_msg_len_copy - total_msg_len) / msg_read_len;
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

	for (int i = 0; i < TIME; i++) {
		while ((msg_len = send(sock, msg, MSG_LENGTH, 0)) > 0)
			total_msg_len += msg_len;
		send_finish = 1;
		while (send_finish)
			;
	}
	// while((msg_len = send(sock, msg, MSG_LENGTH, 0)) > 0)
	//     total_msg_len += msg_len;
	// send_finish = 1;

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
