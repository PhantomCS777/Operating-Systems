1:
	gcc basic_read_write.c -o basic_read_write.out -pthread
	./basic_read_write.out
2:
	gcc write_cache.c -o write_cache.out -pthread
	./write_cache.out
3:
	gcc fifo.c -o fifo.out -pthread
	./fifo.out
4:
	gcc cache_size.c -o cache_size.out -pthread
	./cache_size.out

compile:
	$(MAKE) -C ../
load:
	insmod ../smartbdev.ko
	chmod 666 /proc/smartbdev/cache_size
	echo 4096 > /proc/smartbdev/cache_size
unload:
	rmmod smartbdev
cache_size:
	echo 16384  > /proc/smartbdev/cache_size
large_cache_size:
	echo 131072 > /proc/smartbdev/cache_size 
small_cache_size:
	echo 4096 > /proc/smartbdev/cache_size
stats:
	cat /proc/smartbdev/stats
clean:
	rm -f *.out