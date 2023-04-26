CXX ?= g++

all:server

server: main.cc
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

.PHONY:clean
clean:
	rm  -r server