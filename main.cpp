#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <queue>
#include <cstdlib>
#include <chrono>
#include "tokenizer.h"
#include "index_store.h"
#include "persistence.h"
#include "query_engine.h"

using namespace std;
 namespace fs = std::filesystem;
 using dir_itr = std::filesystem::directory_iterator;

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cout << "ERROR: Please enter corpus path.\n";
        return 1;
    }
    string corpus_path = argv[1];
    string index_file = "core_index.dat";
    auto verify_start = chrono::steady_clock::now();

    size_t current_hash = get_corpus_hash(corpus_path);
    bool rebuild_needed = true;

    if (fs::exists(index_file))
    {
        size_t cached_hash = 0;
        ifstream in(index_file, ios::binary);
        if (in)
        {
            in.read((char *)&cached_hash, sizeof(cached_hash));
            in.close();
        }

        if (cached_hash == current_hash)
        {
            auto verify_end = chrono::steady_clock::now();
            auto verify_dur = chrono::duration_cast<chrono::microseconds>(verify_end - verify_start);
            cout << "\n-> Successfully Cache Verified in " << verify_dur.count() / 1000.0 << " ms.\n";
            cout << "\n[System] Cache is up-to-date. Bypassing text parser...\n";

            auto load_start = chrono::steady_clock::now();
            if (load_index(index_file))
            {
                rebuild_needed = false;
                auto load_end = chrono::steady_clock::now();
                auto load_dur = chrono::duration_cast<chrono::microseconds>(load_end - load_start);
                cout << "-> Successfully loaded " << doc_length.size() << " documents from cache in " << load_dur.count() / 1000.0 << " ms.\n\n";
            }
        }
        else
        {
            cout << "\n\n[System] Corpus changes detected! Invalidating stale cache...\n";
            auto verify_end = chrono::steady_clock::now();
            auto verify_dur = chrono::duration_cast<chrono::microseconds>(verify_end - verify_start);
            cout << "-> Cache Verification Failed in " << verify_dur.count() / 1000.0 << " ms.\n\n";
        }
    }

    if (rebuild_needed)
    {
        cout << "Building Positional Index from scratch: " << corpus_path << "...\n";
        auto build_start = chrono::steady_clock::now();

        build_index_from_corpus(corpus_path);

        auto build_end = chrono::steady_clock::now();
        auto build_dur = chrono::duration_cast<chrono::microseconds>(build_end - build_start);
        cout << "\n-> Successfully built Index of " << doc_length.size() << " documents in " << build_dur.count() / 1000.0 << " ms from scratch.\n";

        // --- METRIC: Indexing Speed (Docs & Tokens per second) ---
        double build_seconds = build_dur.count() / 1000000.0;
        if (build_seconds > 0)
        {
            double docs_per_sec = doc_length.size() / build_seconds;
            double tokens_per_sec = total_indx_words / build_seconds;
            cout << "-> Indexing Speed: " << fixed << setprecision(4) << docs_per_sec << " docs/sec\n";
            cout << "-> Indexing Speed: " << (long long)tokens_per_sec << " tokens/sec\n";
        }

        cout << "\n[System] Parsing complete. Serializing index to disk...\n";

        auto saved_start = chrono::steady_clock::now();
        save_index(index_file, get_corpus_hash(corpus_path));
        auto saved_end = chrono::steady_clock::now();
        auto saved_dur = chrono::duration_cast<chrono::microseconds>(saved_end - saved_start);

        // --- METRIC: Serialization Throughput ---
        double saved_seconds = saved_dur.count() / 1000000.0;
        double mb_saved = fs::file_size(index_file) / (1024.0 * 1024.0);
        cout << "-> Successfully Serialized Index of " << doc_length.size() << " documents in " << saved_dur.count() / 1000.0 << " ms.\n";
        if (saved_seconds > 0)
        {
            cout << "-> Serialization Speed: " << fixed << setprecision(2) << (mb_saved / saved_seconds) << " MiB/sec\n\n";
        }
    }

    double mb = fs::file_size(index_file) / (1024.0 * 1024.0);

    // --- METRIC: Average Postings List Length ---
    // (How many documents a term appears in on average)
    long long total_postings = 0;
    for (const auto &[word, occs] : global_index)
    {
        total_postings += occs.size();
    }
    double avg_postings_len = global_index.empty() ? 0 : (double)total_postings / global_index.size();

    cout << "Index successfully built! Loaded " << doc_length.size() << " documents.\n\n";
    cout << fixed << setprecision(2) << "Serialized cache size: " << mb << " MiB\n\n";
    cout << "Total indexed tokens: " << total_indx_words << "\n\n";
    cout << "Total pages: " << total_pages << "\n\n";
    cout << "Total unique tokens: " << global_index.size() << "\n\n";
    cout << fixed << setprecision(2) << "Average postings list length: " << avg_postings_len << " documents/term\n\n";

    cout << "SearchCore\n\n"
     << "A local document search engine supporting case-insensitive AND, OR, and Exact Phrase queries.\n"
     << "Default mode: AND (no prefix required).\n"
     << "Use the 'or:' prefix for OR queries (e.g. 'or: apple banana').\n"
     << "Use the 'strict:' prefix for exact phrase search (e.g. 'strict: apple banana').\n\n";
    string user_query;
    while (true)
    {
        cout << "Enter search query (or type 'exit' to quit): ";
        getline(cin, user_query);
        long double benchmark_limit = 1000000.0;
        if (user_query == "!benchmark")
        {
            cout << "Enter how many queries to run? ";
            cin >> benchmark_limit;
            cin.ignore(10000, '\n');
            cout << "\n[System] Initiating " << benchmark_limit << "-query benchmark test...\n\n";
            vector<string> all_words;
            for (const auto &[word, occ] : global_index)
                all_words.push_back(word);
            if (all_words.empty())
                continue;

            auto total_start = chrono::steady_clock::now();

            for (int i = 0; i < (benchmark_limit / 10) * 2; i++)
            {
                string random_q = all_words[rand() % all_words.size()];
                search(random_q, true);
            }
            for (int i = 0; i < (benchmark_limit / 10) * 6; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()];
                search(random_q, true);
            }
            for (int i = 0; i < (benchmark_limit / 10) * 2; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()];
                search(random_q, true);
            }

            auto total_end = chrono::steady_clock::now();
            auto total_duration = chrono::duration_cast<chrono::nanoseconds>(total_end - total_start);

            // --- METRIC: Queries Per Second (QPS) ---
            double avg_latency_us = (total_duration.count() / 1000.0) / benchmark_limit;
            double total_seconds = total_duration.count() / 1000000000.00;
            double qps = total_seconds > 0 ? double(benchmark_limit) / total_seconds : 0;

            cout << "[System] Benchmark Complete!\n";
            cout << "-> Total Time for " << benchmark_limit << " queries: " << total_duration.count() / 1000000.0 << " ms\n";
            cout << "-> Average Latency: " << avg_latency_us << " μs per query\n";
            cout << "-> Throughput: " << fixed << setprecision(2) << qps << " Queries Per Second (QPS)\n\n";
            continue;
        }

        if (user_query == "exit")
            break;
        if (user_query.empty())
            continue;

        search(user_query);
    }

    return 0;
}