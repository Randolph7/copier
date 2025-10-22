#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>

int http_msg_size = 0;

enum {
    WAIT,
    START, 
    STOP
};

size_t write_callback(void *contents, size_t size, size_t nmemb, unsigned long *msg_size) {
    size_t realsize = size * nmemb;
    *msg_size += realsize;
    return realsize;
}

void* load_gen(void* status_v){
    char *body = malloc(http_msg_size);
    int* status = (int*)status_v;
    unsigned long msg_size;
    unsigned long count = 0;
    memset(body, '1', http_msg_size - 1);
    body[http_msg_size - 1] = '\0';
    CURL *curl;
    CURLcode res;
    // struct timespec start, end;
    // long long int time_ns;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:8080/echo");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_PROXY, "http://127.0.0.1:8888");
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

    while(*status != START);

    while(1){
        if(*status == STOP){
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return (void*)count;
        }
        // clock_gettime(CLOCK_MONOTONIC, &start);
        msg_size = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&msg_size);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            // fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        // clock_gettime(CLOCK_MONOTONIC, &end);
        // time_ns = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
        // printf("Execution time: %lld ns\n", time_ns);
        count++;

    }
    return 0;
}

int main(int argc, char* argv[]) {
    int thread, time, i, ret, status = WAIT;
    pthread_t* threads;
    struct timespec req, rem;
    int total_count = 0;
    void* thread_rst;

    if(argc != 4){
        printf("thread size time(ms)\n");
        return 1;
    }
    thread = atoi(argv[1]);
    http_msg_size = atoi(argv[2]);
    time = atoi(argv[3]);
    req.tv_sec = 0;
    req.tv_nsec = time * 1000000;

    threads = malloc(sizeof(pthread_t) * thread);

    for(i = 0; i < thread; i++){
        ret = pthread_create(&threads[i], NULL, load_gen, (void*)&status);
        if (ret) {
            fprintf(stderr, "Error - pthread_create() return code: %d\n", ret);
            exit(EXIT_FAILURE);
        }
    }

    status = START;

    nanosleep(&req, &rem);

    status = STOP;

    for(i = 0; i < thread; i++) {
        if (pthread_join(threads[i], &thread_rst) != 0) {
            perror("pthread_join");
            return 1;
        }
        total_count += (unsigned long)thread_rst;
    }

    printf("[throughput] %d RPS\n", total_count * 1000 / time);

    return 0;
}
