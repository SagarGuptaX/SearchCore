build:
	g++ -std=c++20 main.cpp -o search_engine

# Standard run
run: build
	./search_engine corpus


# Memory Profiling (Peak RAM)
ram: build
	echo "exit" | /usr/bin/time -f "\n\nPeak RAM Used: %M KB" ./search_engine corpus


clean:
	rm search_engine

