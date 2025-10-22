# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Redis GET zIO -- results/get-zIO.txt"

touch results/get-zIO.txt

cd redis-6.2-original
make MALLOC=libc
cd ..

redisDir=redis-6.2-original
lines=$(wc -l < results/get-zIO.txt)

jumpto ${lines}

0:
echo "1K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "1K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 1024 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 1024 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

1:
echo "2K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "2K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 2048 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 2048 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

2:
echo "4K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "4K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 4096 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 4096 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

3:
echo "8K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "8K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 8192 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 8192 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

4:
echo "16K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "16K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 16384 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 16384 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

5:
echo "32K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "32K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 32768 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 32768 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

6:
echo "64K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "64K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 65536 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 65536 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

7:
echo "128K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "128K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 131072 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 131072 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

8:
echo "256K benchmark"
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf &
sleep 0.5
echo -n "256K " >> results/get-zIO.txt
numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d 262144 -c 1 -n 1 -p 6379
result=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d 262144 -c 8 -n 800 -p 6379 | tail -n 2 | head -n 1 |  awk '{print $1}' >> results/get-zIO.txt)
pkill redis-server
sleep 0.5

9:
echo "finish Redis GET zIO -- results/get-zIO.txt"



# sudo cget shield --cpu=2 --kthread=on
