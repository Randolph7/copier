# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Recv micro-bench io_uring -- results/recv-io_uring.txt"

touch results/recv-io_uring.txt

cd recv
make
cd ..

lines=$(wc -l < results/recv-io_uring.txt)

jumpto ${lines}

0:
echo "1K benchmark"
echo -n "1K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 1024 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

1:
echo "2K benchmark"
echo -n "2K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 2048 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

2:
echo "4K benchmark"
echo -n "4K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 4096 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

3:
echo "8K benchmark"
echo -n "8K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 8192 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

4:
echo "16K benchmark"
echo -n "16K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 16384 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

5:
echo "32K benchmark"
echo -n "32K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 32768 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

6:
echo "64K benchmark"
echo -n "64K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 65536 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

7:
echo "128K benchmark"
echo -n "128K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 131072 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

8:
echo "256K benchmark"
echo -n "256K " >> results/recv-io_uring.txt
numactl --physcpubind=0 ./recv/io_uring 262144 | awk '{print $13}' >> results/recv-io_uring.txt
sleep 0.5

9:
echo "finish Recv micro-bench io_uring -- results/recv-io_uring.txt"

