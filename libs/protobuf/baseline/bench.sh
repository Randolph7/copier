numactl --physcpubind=0 --membind=0 ./out/cpp-kv-server

sudo cpufreq-set -g performance -c 0
sudo cpufreq-set -g performance -c 1
sudo cpufreq-set -g performance -c 2
sudo cpufreq-set -g performance -c 3

cat /proc/cpuinfo | grep MHz

numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 512
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 1024
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 2048
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 4096
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 8192
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 16384
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 32768
numactl --physcpubind=1 --membind=1 ./out/cpp-kv-client 20 200 65536

