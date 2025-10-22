all:
	gcc normal.c -o normal
	gcc copier.c -o copier
	gcc io_uring.c -luring -o io_uring
	gcc io_uring_batch.c -luring -o io_uring_batch
	gcc io_uring_copier.c -luring -o io_uring_copier
	gcc ub.c -o ub
	gcc z-send.c -o z-send
	gcc z-send-aligned.c -o z-send-aligned

# sudo sysctl -w net.core.rmem_max=16777216
# sudo sysctl -w net.core.wmem_max=16777216

# 128 rounds /512 rounds
