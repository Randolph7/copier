# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

sudo cset shield --cpu=2 --kthread=on

ResultFile=results/set-multi-clients-copier.txt
echo "Redis SET Multi-client Copier 8K/16K -- $ResultFile"

touch $ResultFile

redisDir=redis-6.2-copier-multi-client
benchDir=redis-6.2-copier-multi-client

cd $redisDir
make MALLOC=libc
cd ..

cd $benchDir
make MALLOC=libc
cd ..

lines=$(wc -l < $ResultFile)

jumpto ${lines}

0:
echo "8K benchmark 1 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
sleep 0.5
echo -n "8K-1Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 8192 -c 8 -n 10000 -p 6379 -r 10000000 --threads 1 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

1:
echo "8K benchmark 2 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
sleep 0.5
numactl --physcpubind=4 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-2.conf &
sleep 0.5
echo -n "8K-2Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 8192 -c 16 -n 20000 -p 6379 -r 10000000 --threads 2 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

2:
echo "8K benchmark 3 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
numactl --physcpubind=4 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-2.conf &
numactl --physcpubind=6 ./redis-6.2-original/src/redis-server redis-6.2-original/config/non-persistent-3.conf &
sleep 0.5
echo -n "8K-3Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 8192 -c 24 -n 30000 -p 6379 -r 10000000 --threads 3 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

3:
echo "16K benchmark 1 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
sleep 0.5
echo -n "16K-1Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 16384 -c 8 -n 10000 -p 6379 -r 10000000 --threads 1 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

4:
echo "16K benchmark 2 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
numactl --physcpubind=4 ./redis-6.2-original/src/redis-server redis-6.2-original/config/non-persistent-2.conf &
sleep 0.5
echo -n "16K-2Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 16384 -c 16 -n 20000 -p 6379 -r 10000000 --threads 2 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

5:
echo "16K benchmark 3 Redis"
./${redisDir}/src/setup_cp_thread
numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent-1.conf &
numactl --physcpubind=4 ./redis-6.2-original/src/redis-server redis-6.2-original/config/non-persistent-2.conf &
numactl --physcpubind=6 ./redis-6.2-original/src/redis-server redis-6.2-original/config/non-persistent-3.conf &
sleep 0.5
echo -n "16K-3Redis " >> $ResultFile
result=$(numactl --cpubind=1 ./${benchDir}/src/redis-benchmark -t set -d 16384 -c 24 -n 30000 -p 6379 -r 10000000 --threads 3 | tail -n 5 | awk 'NR==1 {word1=$3} NR==4 {word2=$1} END {print word1, word2}' >> $ResultFile)
pkill redis-server
./${redisDir}/src/down_cp_thread
sleep 0.5

6:
echo "finish Redis SET Multi-client Copier 8K/16K -- $ResultFile"
