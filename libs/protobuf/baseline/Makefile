all:
	# protoc kv.proto --cpp_out=./cpp-kv --grpc_out=./cpp-kv --plugin=protoc-gen-grpc=/home/hjk/grpc/bin/grpc_cpp_plugin #--go_out=./go-kv
	
	protoc kv.proto --cpp_out=./cpp-kv
	mkdir -p out
	pkg-config --cflags protobuf
	g++ cpp-kv/kv-client.cc cpp-kv/copier.cc cpp-kv/kv.pb.cc -o out/cpp-kv-client `pkg-config --cflags --libs protobuf`
	g++ cpp-kv/copier.cc cpp-kv/kv-server.cc cpp-kv/array_input_stream.cc cpp-kv/kv.pb.cc -o out/cpp-kv-server `pkg-config --cflags --libs protobuf` 
