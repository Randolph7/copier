# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Send micro-bench io_uring_batch -- results/send-io_uring_batch.txt"
echo "4096 629145600 629145600" | sudo tee /proc/sys/net/ipv4/tcp_rmem
echo "4096 629145600 629145600" | sudo tee /proc/sys/net/ipv4/tcp_wmem

touch results/send-io_uring_batch.txt

cd send
make
cd ..

lines=$(wc -l < results/send-io_uring_batch.txt)

jumpto ${lines}

0:
echo "1K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 1024 512 | awk '{print $13}')
echo -n "1K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

1:
echo "2K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 2048 512 | awk '{print $13}')
echo -n "2K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

2:
echo "4K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 4096 512 | awk '{print $13}')
echo -n "4K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

3:
echo "8K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 8192 512 | awk '{print $13}')
echo -n "8K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

4:
echo "16K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 16384 128 | awk '{print $13}')
echo -n "16K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

5:
echo "32K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 32768 128 | awk '{print $13}')
echo -n "32K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

6:
echo "64K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 65536 64 | awk '{print $13}')
echo -n "64K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

7:
echo "128K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 131072 64 | awk '{print $13}')
echo -n "128K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

8:
echo "256K benchmark"
num=$(numactl --physcpubind=0 ./send/io_uring_batch 262144 64 | awk '{print $13}')
echo -n "256K " >> results/send-io_uring_batch.txt
echo $num >> results/send-io_uring_batch.txt
sleep 0.5

9:
echo "finish Send micro-bench io_uring_batch -- results/send-io_uring_batch.txt"

