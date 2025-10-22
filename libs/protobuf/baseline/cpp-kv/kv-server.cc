

#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <poll.h>

#include "def.h"
#include "kv.pb.h"
#include "copier.h"
#include "array_input_stream.h"

namespace KVStorage
{
class KVStorage {
    public:
	std::unordered_map<std::uint64_t, std::shared_ptr<kv::KV> > store;
	KVStorage()= default;
};
} // namespace KVStorage

int threadFd;
int server_fd;

void signalHandler(int signum) {
	close(server_fd);
    copier::delCpThread(-1, threadFd, QUEUE_TYPE_OUT);
    exit(signum);  
}

int main()
{
#ifdef COPIER_RECV
	signal(SIGINT, signalHandler);
#endif
	int client_sock;
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
	if (listen(server_fd, 10) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

#ifdef COPIER_RECV
	threadFd = copier::createCpThread(-1, RECV_COPIER_THREAD_CORE, QUEUE_TYPE_OUT);
    struct copier::syncQueueCtx * ctx = copier::mmapSyncQueue(threadFd);
#endif

	KVStorage::KVStorage storage;

	while (true) {
		uint64_t messageLength, totalEpho;
		std::int64_t key;

		if ((client_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
			perror("accept");
			continue;
		}
#ifdef COPIER_RECV
		copier::bindCpThread(client_sock, threadFd, QUEUE_TYPE_OUT);
#endif
		size_t bytes_received = recv(client_sock, &messageLength, sizeof(uint64_t), 0);
		if (bytes_received != sizeof(uint64_t))
			perror("messageLength must be uint64_t");
		send(client_sock, &messageLength, sizeof(std::int64_t), 0);

		bytes_received = recv(client_sock, &totalEpho, sizeof(uint64_t), 0);
		if (bytes_received != sizeof(uint64_t))
			perror("totalEpho must be uint64_t");
		send(client_sock, &totalEpho, sizeof(std::int64_t), 0);

		uint8_t buffer[messageLength];
#ifdef COPIER_RECV
        copier::smartInputDescriptorBuffer descriptorBuffer{.current_input_end = 0, .ctx = ctx};
#endif
		uint64_t currentOffset = 0, currentEpho = 0;

		std::chrono::high_resolution_clock::time_point start, end;
		
		struct pollfd fds[1];
    	fds[0].fd = client_sock;
    	fds[0].events = POLLIN;

		
		while(currentEpho < totalEpho){
			int poll_count = poll(fds, 1, 500);
			start = std::chrono::high_resolution_clock::now();
    
    		if (poll_count == -1 || poll_count == 0) {
    			close(client_sock);
        		break;
			}

        	if (fds[0].revents & POLLIN) {
				while(1){
#ifdef COPIER_RECV
					bytes_received = copier::recvWithsmartInputDescriptorBufferRedis(client_sock, buffer, currentOffset, messageLength, 0, &descriptorBuffer);
#else
					bytes_received = recv(client_sock, buffer + currentOffset, messageLength, 0);
#endif
					currentOffset += bytes_received;
					if (currentOffset == messageLength)
						break;
				}
				currentOffset = 0;
				currentEpho++;
				auto kv = std::make_shared<kv::KV>();
#ifdef COPIER_RECV
                google::protobuf::io::ArrayInputStreamCopier arrayInputStream(buffer, messageLength, 4096, &descriptorBuffer);
#else
				google::protobuf::io::ArrayInputStream arrayInputStream(buffer, messageLength, 4096);
#endif
				kv->ParseFromZeroCopyStream(&arrayInputStream);
				key = kv->key();
				storage.store[key] = kv;
				end = std::chrono::high_resolution_clock::now();
				long long duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
				send(client_sock, &duration, sizeof(std::int64_t), 0);
			}
        }
// 		while (currentEpho < totalEpho) {
// #ifdef COPIER_RECV
// 			bytes_received = copier::recvWithsmartInputDescriptorBufferRedis(client_sock, buffer, currentOffset, messageLength, 0, &descriptorBuffer);
// #else
// 			bytes_received = recv(client_sock, buffer + currentOffset, messageLength, 0);
// #endif
// 			if (bytes_received <= 0) {
// 				close(client_sock);
// 				break;
// 			}
// 			currentOffset += bytes_received;
// 			if (currentOffset == messageLength) {
// 				currentOffset = 0;
// 				currentEpho++;
// 				auto kv = std::make_shared<kv::KV>();
// #ifdef COPIER_RECV
//                 google::protobuf::io::ArrayInputStreamCopier arrayInputStream(buffer, messageLength, 1024, &descriptorBuffer);
// #else
// 				google::protobuf::io::ArrayInputStream arrayInputStream(buffer, messageLength, 1024);
// #endif
// 				kv->ParseFromZeroCopyStream(&arrayInputStream);
// 				key = kv->key();
// 				storage.store[key] = kv;
// 				send(client_sock, &key, sizeof(std::int64_t), 0);
// 			}
// 		}
	}
	close(server_fd);
	return 0;
}