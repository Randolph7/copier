all:
	mkdir -p out
	go build -o out/server server/main.go
	gcc client/client.c -o out/client -lpthread -lcurl
	# gcc server/server.c server/mongoose.c -o out/server -lpthread
