# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Send micro-bench Baseline -- results/send-baseline.txt"

touch results/send-baseline.txt
echo "4096 629145600 629145600" | sudo tee /proc/sys/net/ipv4/tcp_rmem
echo "4096 629145600 629145600" | sudo tee /proc/sys/net/ipv4/tcp_wmem

cd send
make
cd ..

lines=$(wc -l < results/send-baseline.txt)

jumpto ${lines}

0:
echo "1K benchmark"
echo -n "1K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 1024 512 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

1:
echo "2K benchmark"
echo -n "2K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 2048 512 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

2:
echo "4K benchmark"
echo -n "4K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 4096 512 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

3:
echo "8K benchmark"
echo -n "8K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 8192 512 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

4:
echo "16K benchmark"
echo -n "16K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 16384 128 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

5:
echo "32K benchmark"
echo -n "32K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 32768 128 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

6:
echo "64K benchmark"
echo -n "64K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 65536 64 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

7:
echo "128K benchmark"
echo -n "128K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 131072 64 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

8:
echo "256K benchmark"
echo -n "256K " >> results/send-baseline.txt
numactl --physcpubind=0 ./send/normal 262144 64 | awk '{print $13}' >> results/send-baseline.txt
sleep 0.5

9:
echo "finish Send micro-bench Baseline -- results/send-baseline.txt"

