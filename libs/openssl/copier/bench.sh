echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_rmem
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_wmem

./config --prefix=/home/hjk/copier/copier-motiv-eval/openssl-ins
LD_PRELOAD="/home/hjk/copier/copier-motiv-eval/openssl-ins/lib64/libcrypto.so /home/hjk/copier/copier-motiv-eval/openssl-ins/lib64/libssl.so" numactl --physcpubind=0 --membind=0  ./server
LD_PRELOAD="/home/hjk/copier/copier-motiv-eval/openssl-ins-o/lib64/libcrypto.so /home/hjk/copier/copier-motiv-eval/openssl-ins-o/lib64/libssl.so" numactl --physcpubind=0 --membind=0 ./server

numactl --physcpubind=1 --membind=1 ./client 16384

sudo cpufreq-set -g performance -c 0
sudo cpufreq-set -g performance -c 1
sudo cpufreq-set -g performance -c 2

cat /proc/cpuinfo | grep MHz
