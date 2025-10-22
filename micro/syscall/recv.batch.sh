# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Recv micro-bench io_uring_batch -- results/recv-io_uring_batch.txt"
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_rmem
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_wmem

touch results/recv-io_uring_batch.txt

cd recv
make
cd ..

lines=$(wc -l < results/recv-io_uring_batch.txt)

jumpto ${lines}

0:
echo "1K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 1024 | awk '{print $13}')
if [ ${#num} -lt 5 ]; then
    echo -n "1K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 0
fi
sleep 0.5

1:
echo "2K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 2048 | awk '{print $13}')
if [ ${#num} -lt 5 ]; then
    echo -n "2K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 1
fi
sleep 0.5

2:
echo "4K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 4096 | awk '{print $13}')
if [ ${#num} -lt 5 ]; then
    echo -n "4K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 2
fi
sleep 0.5

3:
echo "8K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 8192 | awk '{print $13}')
if [ ${#num} -lt 5 ]; then
    echo -n "8K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 3
fi
sleep 0.5

4:
echo "16K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 16384 | awk '{print $13}')
if [ ${#num} -lt 6 ]; then
    echo -n "16K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 4
fi
sleep 0.5

5:
echo "32K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 32768 | awk '{print $13}')
if [ ${#num} -lt 6 ]; then
    echo -n "32K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 5
fi
sleep 0.5

6:
echo "64K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 65536 | awk '{print $13}')
if [ ${#num} -lt 6 ]; then
    echo -n "64K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 6
fi
sleep 0.5

7:
echo "128K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 131072 | awk '{print $13}')
if [ ${#num} -lt 6 ]; then
    echo -n "128K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 7
fi
sleep 0.5

8:
echo "256K benchmark"
num=$(numactl --physcpubind=0 ./recv/io_uring_batch 262144 | awk '{print $13}')
if [ ${#num} -lt 6 ]; then
    echo -n "256K " >> results/recv-io_uring_batch.txt
    echo $num >> results/recv-io_uring_batch.txt
else
    sleep 0.5
    jumpto 8
fi
sleep 0.5

9:
echo "finish Recv micro-bench io_uring_batch -- results/recv-io_uring_batch.txt"

