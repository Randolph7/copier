# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "TinyProxy Baseline (multi-threading) -- results/threads.baseline.txt"
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_rmem

touch results/threads.baseline.txt

tinyProxyDir=tinyproxy-baseline-threads
cd $tinyProxyDir
./autogen.sh
make
cd ..

cd http-proxy-bench
make
cd ..

./http-proxy-bench/out/server &> /dev/null &

lines=$(wc -l < results/threads.baseline.txt)

jumpto ${lines}

0:
echo "2 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "2 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 2 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

1:
echo "4 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "4 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 4 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

2:
echo "6 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "6 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 6 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

3:
echo "8 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "8 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 8 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

4:
echo "10 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "10 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 10 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

5:
echo "12 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "12 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 12 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

6:
echo "14 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "14 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 14 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

7:
echo "16 threads"
./$tinyProxyDir/src/tinyproxy -c $tinyProxyDir/etc/tinyproxy.bench.conf -d &> /dev/null &
sleep 0.5
echo -n "16 " >> results/threads.baseline.txt
result=$(./http-proxy-bench/out/client 16 8192 200 | awk '{print $2}' >> results/threads.baseline.txt)
pkill tinyproxy
sleep 0.5

8:
echo "finish TinyProxy Baseline (multi-threading) -- results/threads.baseline.txt"

sudo pkill server
