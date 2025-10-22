./src/redis-benchmark -t set -d 512 -c 1 -n 100 -p 6379 #512
./src/redis-benchmark -t set -d 1024 -c 1 -n 100 -p 6379 #1k
./src/redis-benchmark -t set -d 4096 -c 1 -n 100 -p 6379 #4k
./src/redis-benchmark -t set -d 16384 -c 1 -n 100 -p 6379 #16k
./src/redis-benchmark -t set -d 32768 -c 1 -n 100 -p 6379 #32k
./src/redis-benchmark -t set -d 65536 -c 1 -n 100 -p 6379 #64k
./src/redis-benchmark -t set -d 262144 -c 1 -n 100 -p 6379 #256k
./src/redis-benchmark -t set -d 524288 -c 1 -n 100 -p 6379 #512k
./src/redis-benchmark -t set -d 1048576 -c 1 -n 100 -p 6379 #1M
./src/redis-benchmark -t set -d 4194304 -c 1 -n 100 -p 6379 #4M

./src/setup_cp_thread
./src/redis-server config/non-persistent-1.conf
./src/redis-benchmark -t set -d 16384 -c 8 -n 800 -p 6379
./src/down_cp_thread
