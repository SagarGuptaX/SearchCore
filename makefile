build:
	g++ -std=c++17 main.cpp -o search_engine

# Standard run
run: build
	./search_engine corpus


# Memory Profiling (Peak RAM)
profile: build
	/usr/bin/time -v ./search_engine corpus

clean:
	rm search_engine