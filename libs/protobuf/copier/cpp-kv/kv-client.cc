#include <arpa/inet.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <google/protobuf/descriptor.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <csignal>

#include "def.h"
#include "kv.pb.h"
#include "copier.h"

const char charset[] = "abcdefghijklmnopqrstuvwxyz";
const size_t max_index = sizeof(charset) - 2;
int threadFd;

char randomChar()
{
	return charset[rand() % max_index];
}

void signalHandler(int signum) {
    copier::delCpThread(-1, threadFd, QUEUE_TYPE_OUT);
    exit(signum);
}

int main(int argc, char *argv[])
{
#ifdef COPIER_SEND
	signal(SIGINT, signalHandler);
#endif
	if (argc != 4) {
		std::cerr << "Usage: " << argv[0] << " prewarmEpho evaluationEpho valueSize" << std::endl;
		return 1;
	}

	const int prewarmEpho = std::atoi(argv[1]);
	const int evaluationEpho = std::atoi(argv[2]);
	const int valueSize = std::atoi(argv[3]);

	int sock = 0;
	struct sockaddr_in serv_addr;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		std::cerr << "Socket creation error" << std::endl;
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
		std::cerr << "Invalid address / Address not supported" << std::endl;
		return -1;
	}

	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		std::cerr << "Connection Failed" << std::endl;
		return -1;
	}

#ifdef COPIER_SEND
	threadFd = copier::createCpThread(-1, SEND_COPIER_THREAD_CORE, QUEUE_TYPE_IN);
	copier::bindCpThread(sock, threadFd, QUEUE_TYPE_IN);
#endif

	std::vector<kv::KV> kvs;
	std::vector<std::string> codedStrings;
	std::vector<long long> recordedDurations(evaluationEpho, 0);

	for (uint64_t i = 0; i < prewarmEpho + evaluationEpho; i++) {
		kv::KV kv;
		std::string str(valueSize, 0);
		std::generate_n(str.begin(), valueSize, randomChar);
		kv.set_key(200 + i);
		kv.set_value(str);
	
		kvs.push_back(kv);
	}

	for (uint64_t i = 0; i < prewarmEpho + evaluationEpho; i++) {
		std::string codedString;
		kvs[i].SerializeToString(&codedString);
		codedStrings.push_back(codedString);
	}

	size_t serializedStringLen = codedStrings[0].size();
	for (uint64_t i = 1; i < prewarmEpho + evaluationEpho; i++)
		if (serializedStringLen != codedStrings[i].size()) {
			perror("length is not equal, please reassign the base\n");
			exit(0);
		}

	size_t replySerializedStringLen;
	send(sock, &serializedStringLen, sizeof(size_t), 0);
	recv(sock, &replySerializedStringLen, sizeof(size_t), 0);
	assert(replySerializedStringLen == serializedStringLen);

	size_t totalEpho = prewarmEpho + evaluationEpho, replyTotalEpho;
	send(sock, &totalEpho, sizeof(size_t), 0);
	recv(sock, &replyTotalEpho, sizeof(size_t), 0);
	assert(totalEpho == replyTotalEpho);

	std::int64_t keyRecv;

	for (int i = 0; i < prewarmEpho; i++) {
#ifdef COPIER_SEND
		sendWithCopier(sock, codedStrings[i].c_str(), codedStrings[i].size(), 0);
#else
		send(sock, codedStrings[i].c_str(), codedStrings[i].size(), 0);
#endif
		recv(sock, &keyRecv, sizeof(std::int64_t), 0);
	}

	for (int i = prewarmEpho; i < prewarmEpho + evaluationEpho; i++) {
#ifdef COPIER_SEND
		sendWithCopier(sock, codedStrings[i].c_str(), codedStrings[i].size(), 0);
#else
		send(sock, codedStrings[i].c_str(), codedStrings[i].size(), 0);
#endif
		recv(sock, &keyRecv, sizeof(std::int64_t), 0);
		recordedDurations[i - prewarmEpho] = keyRecv;
	}

	long long max = *std::max_element(recordedDurations.begin(), recordedDurations.end());
	std::cout << "Max value: " << max << std::endl;

	long long min = *std::min_element(recordedDurations.begin(), recordedDurations.end());
	std::cout << "Min value: " << min << std::endl;

	long long sum = std::accumulate(recordedDurations.begin(), recordedDurations.end(), 0LL);
	double average = static_cast<double>(sum) / recordedDurations.size();
    std::cout << "Average value: " << average << std::endl;

	size_t p99Index = static_cast<size_t>(std::ceil(0.99 * recordedDurations.size())) - 1;
	std::nth_element(recordedDurations.begin(), recordedDurations.begin() + p99Index, recordedDurations.end());
	long long p99Value = recordedDurations[p99Index];
	std::cout << "P99 value: " << p99Value << std::endl;

    std::cout << "Throughput: " << (float)evaluationEpho / (float)std::accumulate(recordedDurations.begin(), recordedDurations.end(), 0) *1000000000 << std::endl;

#ifdef COPIER_SEND
	copier::delCpThread(sock, threadFd, QUEUE_TYPE_IN);
#endif
	close(sock);

	return 0;
}
