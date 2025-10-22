# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "TinyProxy Baseline (single-threaded) -- results/size.baseline.txt"
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_rmem

touch results/size.baseline.txt

tinyProxyDir=tinyproxy-baseline-size
cd $tinyProxyDir
./autogen.sh
make
cd ..

cd http-proxy-bench
make
cd ..

numactl --physcpubind=1 ./http-proxy-bench/out/server &> /dev/null &

lines=$(wc -l < results/size.baseline.txt)

jumpto ${lines}

0:
echo "1K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "1K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 1024 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

1:
echo "2K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "2K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 2048 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

2:
echo "4K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "4K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 4096 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

3:
echo "8K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "8K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 8192 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

4:
echo "16K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "16K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 16384 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

5:
echo "32K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "32K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 32768 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

6:
echo "64K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "64K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 65536 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

7:
echo "128K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "128K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 131072 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

8:
echo "256K benchmark"
numactl --physcpubind=0 ./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "256K " >> results/size.baseline.txt
result=$(numactl --physcpubind=3 ./http-proxy-bench/out/client 1 262144 200 | awk '{print $2}' >> results/size.baseline.txt)
pkill tinyproxy
sleep 0.5

9:
echo "finish TinyProxy Baseline (single-threaded) -- results/size.baseline.txt"

sudo pkill server
