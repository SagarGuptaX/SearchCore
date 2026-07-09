build:
	g++ -std=c++20 -O2 -flto tokenizer.cpp index_store.cpp persistence.cpp query_engine.cpp main.cpp -o search_engine

# Standard run
run: build
	/usr/bin/time -f "\n\nPeak RAM Used: %M KB" ./search_engine corpus


# Memory Profiling (Peak RAM)
ram: build
	echo "exit" | /usr/bin/time -f "\n\nPeak RAM Used: %M KB" ./search_engine corpus


clean:
	rm search_engine

