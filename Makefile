CC = g++
CFLAGS = -std=c++11 -Wall -O2
LDFLAGS = -lrt

TARGETS = proc_a proc_b

.PHONY: all clean

all: $(TARGETS)

proc_a: proc_a.cpp src/mul_process/shm_manager.cpp src/mul_process/shm_manager.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

proc_b: proc_b.cpp src/mul_process/shm_manager.cpp src/mul_process/shm_manager.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/shm_lockfree_ring