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
#include <chrono> // For profiling our engine's speed

using namespace std;
namespace fs = std::filesystem;
using dir_itr = std::filesystem::directory_iterator;

// ==========================================
// STOP WORDS DICTIONARY (Domain-Specific)
// ==========================================
unordered_set<string> stop_words = {
    // Legal boilerplate — very high frequency, low value
    "hereby", "herein", "thereof", "thereto", "therein", "hereunder",
    "whereas", "pursuant", "aforesaid", "aforementioned", "hereinafter",
    "notwithstanding", "thereupon", "heretofore",
    // Articles & Determiners
    "the", "a", "an", "this", "that", "these", "those", "each", "every", "all", "any",
    // Prepositions
    "of", "to", "in", "for", "on", "at", "by", "from", "with", "into", "about",
    "upon", "under", "over", "between", "through", "against", "during", "within",
    // Conjunctions
    "and", "or", "but", "so", "if", "as", "than", "nor",
    // Verbs (auxiliary / linking)
    "is", "are", "was", "were", "be", "been", "being",
    "has", "have", "had", "do", "did", "does", "will", "would",
    "may", "can", "shall", "should", "could", "might",
    // Pronouns
    "it", "its", "we", "our", "they", "their", "he", "she", "you", "i",
    // Common adverbs / others
    "not", "no", "also", "such", "more", "other", "said", "up", "out", "then",
    "than", "however"
};

// ==========================================
// GLOBAL ARCHITECTURE (Positional V2)
// ==========================================

struct Occurrence {
    int docID; 
    int total_frequency; 
    unordered_map<int, vector<int>> page_positions; 
};

unordered_map<string, vector<Occurrence>> global_index;
vector<string> ID_allocator;
vector<int> doc_length;

// ==========================================
// STAGE 2: O(N) TWO-POINTER INTERSECTION
// ==========================================
vector<int> intersect(const vector<Occurrence> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0; 
    while (i < list1.size() && j < list2.size()) {
        if (list1[i].docID == list2[j].docID) {
            matched_docs.push_back(list1[i].docID);
            i++; j++;
        }
        else if (list1[i].docID < list2[j].docID) i++;
        else j++;
    }
    return matched_docs;
}

vector<int> intersect(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0; 
    while (i < list1.size() && j < list2.size()) {
        if (list1[i] == list2[j].docID) {
            matched_docs.push_back(list1[i]);
            i++; j++;
        }
        else if (list1[i] < list2[j].docID) i++;
        else j++;
    }
    return matched_docs;
}

vector<int> union_docs(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> merged_docs;
    int i = 0, j = 0; 
    while (i < list1.size() && j < list2.size()) {
        if (list1[i] == list2[j].docID) {
            merged_docs.push_back(list1[i]);
            i++; j++;
        }
        else if (list1[i] < list2[j].docID) {
            merged_docs.push_back(list1[i]);
            i++;
        }
        else {
            merged_docs.push_back(list2[j].docID);
            j++;
        }
    }
    while (i < list1.size()) merged_docs.push_back(list1[i++]);
    while (j < list2.size()) merged_docs.push_back(list2[j++].docID);
    return merged_docs;
}

double calculate_slop_multiplier(int docID, const vector<string>& tokens) {
    if (tokens.size() < 2) return 1.0; 

    double total_multiplier = 1.0;

    for (size_t i = 0; i < tokens.size() - 1; i++) {
        string word1 = tokens[i];
        string word2 = tokens[i+1];

        // Memory Safety: Do not insert missing words into global_index
        if (!global_index.count(word1) || !global_index.count(word2)) continue;

        const Occurrence* occ1 = nullptr;
        const Occurrence* occ2 = nullptr;

        for (const auto& o : global_index.at(word1)) { if (o.docID == docID) { occ1 = &o; break; } }
        for (const auto& o : global_index.at(word2)) { if (o.docID == docID) { occ2 = &o; break; } }

        if (!occ1 || !occ2) continue; 

        int min_distance = 999999;
        bool correct_direction = false;

        for (const auto& [page, pos_list1] : occ1->page_positions) {
            if (occ2->page_positions.count(page)) {
                const auto& pos_list2 = occ2->page_positions.at(page);
                for (int p1 : pos_list1) {
                    for (int p2 : pos_list2) {
                        int dist = abs(p2 - p1);
                        if (dist < min_distance) {
                            min_distance = dist;
                            correct_direction = (p2 > p1); 
                        }
                    }
                }
            }
        }

        double pair_multiplier = 1.0;
        if (min_distance == 1) pair_multiplier = 2.0;
        else if (min_distance <= 5) pair_multiplier = 1.5;
        else if (min_distance <= 15) pair_multiplier = 1.2;

        if (pair_multiplier > 1.0 && correct_direction) {
            pair_multiplier *= 1.2;
        }
        total_multiplier *= pair_multiplier;
    }
    
    return total_multiplier;
}

// ==========================================
// STAGE 3: SEARCH & BM25 RANKING
// ==========================================
void search(string query, bool silent=false)
{
    auto start_time = chrono::steady_clock::now();

    vector<string> tokens;
    bool is_or_query = false;
    if (query.length() >= 3 && 
        tolower(query[0]) == 'o' && 
        tolower(query[1]) == 'r' && 
        query[2] == ':') 
    {
        is_or_query = true;
        query = query.substr(3); 
    }
    
    std::replace(query.begin(), query.end(), '-', ' ');
    stringstream ss(query);
    string word;

    while (ss >> word)
    {
        word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return std::tolower(c); });
        if (stop_words.count(word)) continue;
        if (!word.empty()) {
            tokens.push_back(word);
        }
    }

    if (tokens.empty()) {
        if (!silent) cout << "Query only contained stop-words (no meaningful words) or punctuation.\n";
        return;
    }

    if (tokens.size() > 10) {
        if (!silent) cout << "[WARNING] Query too long. Truncating to first 10 words.\n";
        tokens.resize(10);
    }

    vector<int> matched_docs;

    // Safely load the first word's documents using .count() and .at()
    if (global_index.count(tokens[0])) {
        for (auto &occ : global_index.at(tokens[0])) {
            matched_docs.push_back(occ.docID);
        }
    }

    for (size_t i = 1; i < tokens.size(); i++) {
        if (matched_docs.empty() && !is_or_query) break;

        if (is_or_query) {
            if (global_index.count(tokens[i])) {
                matched_docs = union_docs(matched_docs, global_index.at(tokens[i]));
            }
        } else {
            if (global_index.count(tokens[i])) {
                matched_docs = intersect(matched_docs, global_index.at(tokens[i]));
            } else {
                matched_docs.clear(); // Word doesn't exist, AND query instantly fails
                break;
            }
        }
    }
    
    if (matched_docs.empty()) {
        if (!silent) cout << "No matching documents found.\n";
        return;
    }

    double avg_doc_len = 0.0;
    for (const int &x : doc_length) avg_doc_len += x;
    avg_doc_len /= (double)doc_length.size();

    double k1 = 1.2;              
    double b = 0.75;              
    double N_docs = doc_length.size(); 

    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> top_k_queue;
    int MAX_RESULTS = 50; 

    for (int docID : matched_docs)
    {
        double total_score = 0.0;

        for (string q : tokens)
        {
            if (!global_index.count(q)) continue; // Memory safety

            double n_q = global_index.at(q).size();
            double idf = log((N_docs - n_q + 0.5) / (n_q + 0.5) + 1.0);

            double f_q_D = 0.0;
            for (auto &occ : global_index.at(q))
            {
                if (occ.docID == docID) {
                    f_q_D = occ.total_frequency; 
                    break; 
                }
            }

            double dl = doc_length[docID];
            double numerator = f_q_D * (k1 + 1.0);
            double denominator = f_q_D + k1 * (1.0 - b + b * (dl / avg_doc_len));
            total_score += idf * (numerator / denominator);
        }

        double slop_multiplier = calculate_slop_multiplier(docID, tokens);
        total_score *= slop_multiplier; 

        top_k_queue.push({total_score, docID});
        if (top_k_queue.size() > MAX_RESULTS) {
            top_k_queue.pop(); 
        }
    }

    vector<pair<double, int>> doc_scores;
    while (!top_k_queue.empty()) {
        doc_scores.push_back(top_k_queue.top());
        top_k_queue.pop();
    }
    std::reverse(doc_scores.begin(), doc_scores.end());

    auto end_time = chrono::steady_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time);

    // [SHORT CIRCUIT FOR BENCHMARKS]
    if (silent) {
        return; 
    }

    // 5. Pagination UI & Linker (All redundant `if (!silent)` removed from here down)
    cout << "\n--- Top " << doc_scores.size() << " Search Results ---\n";
    
    int PAGE_SIZE = 10;
    int current_index = 0;
    string user_input = "";

    while (current_index < doc_scores.size()) {
        int end_index = min(current_index + PAGE_SIZE, (int)doc_scores.size());

        for (int i = current_index; i < end_index; i++) {
            int docID = doc_scores[i].second;
            double score = doc_scores[i].first;
            string file_path = ID_allocator[docID];

            unordered_map<int, int> page_hits;
            for (string q : tokens) {
                if (!global_index.count(q)) continue;
                for (auto &occ : global_index.at(q)) {
                    if (occ.docID == docID) {
                        for (auto const& [page, pos_list] : occ.page_positions) {
                            page_hits[page] += pos_list.size();
                        }
                        break; 
                    }
                }
            }

            int best_page = 999999; 
            int max_hits = -1;
            vector<int> all_pages;
            
            for (auto const& [page, hits] : page_hits) {
                all_pages.push_back(page);
                if (hits > max_hits || (hits == max_hits && page < best_page)) {
                    max_hits = hits;
                    best_page = page;
                }
            }
            
            all_pages.erase(std::remove(all_pages.begin(), all_pages.end(), best_page), all_pages.end());

            string filename = fs::path(file_path).filename().string();
            cout << i + 1 << ". " << filename << " (Score: " << score << ")\n";
            
            if (best_page != 999999) {
                cout << "   ↳ Best Match: Open directly to Page " << best_page 
                     << " (file://" << file_path << ")\n";
            }
            
            if (!all_pages.empty()) {
                cout << "   ↳ Also mentioned on pages: ";
                int max_pages_to_show = min(5, (int)all_pages.size()); 
                for (int p = 0; p < max_pages_to_show; p++) {
                    cout << all_pages[p] << (p == max_pages_to_show - 1 ? "" : ", ");
                }
                if (all_pages.size() > 5) cout << "...";
                cout << "\n";
            }
        }

        current_index += PAGE_SIZE;

        if (current_index < doc_scores.size()) {
            cout << "\n[Press ENTER to show next 10 results, or type 'q' to start a new search] ";
            getline(cin, user_input);
            if (user_input == "q" || user_input == "Q") break;
        }
    }
    cout << "------------------------------\n";
    cout << "Search latency: " << duration.count() / 1000.0 << " ms\n";
}

// ==========================================
// STAGE 1: MAIN EXECUTION & INDEXING
// ==========================================
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cout << "ERROR: Please enter corpus path.\n";
        return 1;
    }

    string corpus_path = argv[1];
    cout << "Building Positional Index from: " << corpus_path << "...\n";

    for (const auto &file : dir_itr(corpus_path))
    {
        int fileID = ID_allocator.size();
        ID_allocator.push_back(file.path().string());

        std::ifstream in_file(file.path().string());

        if (in_file.is_open())
        {
            int total_words = 0;
            string raw_word;
            
            unordered_map<string, Occurrence> local_word_tracker;
            int current_page_number = 1;
            int absolute_word_position = 0; 

            while (in_file >> raw_word)
            {
                if (raw_word.find('\f') != string::npos) {
                    current_page_number++;
                    absolute_word_position = 0; 
                    raw_word.erase(std::remove(raw_word.begin(), raw_word.end(), '\f'), raw_word.end());
                }

                std::replace(raw_word.begin(), raw_word.end(), '-', ' ');
                stringstream word_ss(raw_word);
                string word;
                
                while (word_ss >> word) 
                {
                    word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
                    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (stop_words.count(word)) continue;

                    if (!word.empty())
                    {
                        absolute_word_position++;
                        total_words++;

                        if (local_word_tracker[word].total_frequency == 0) {
                            local_word_tracker[word].docID = fileID;
                        }
                        
                        local_word_tracker[word].total_frequency++;
                        local_word_tracker[word].page_positions[current_page_number].push_back(absolute_word_position);
                    }
                }
            }

            doc_length.push_back(total_words);

            for (const auto &[per_word, occ_data] : local_word_tracker)
            {
                global_index[per_word].push_back(occ_data);
            }
        }
    }

    cout << "Index successfully built! Loaded " << doc_length.size() << " documents.\n\n";

    string user_query;
    while (true)
    {
        cout << "Enter search query (or type 'exit' to quit): ";
        getline(cin, user_query);
        
        if (user_query == "!benchmark") {
            cout << "\n[System] Initiating 1,000-query benchmark test...\n";
            
            vector<string> all_words;
            for (const auto& [word, occ] : global_index) {
                all_words.push_back(word);
            }
            
            if (all_words.empty()) continue;

            auto total_start = chrono::steady_clock::now();
            int num_queries = 1000;

            for (int i = 0; i < num_queries; i++) {
                string random_q = all_words[rand() % all_words.size()] + " " + 
                                  all_words[rand() % all_words.size()];
                search(random_q, true); 
            }

            auto total_end = chrono::steady_clock::now();
            auto total_duration = chrono::duration_cast<chrono::microseconds>(total_end - total_start);
            
            double avg_latency_ms = (total_duration.count() / 1000.0) / num_queries;
            
            cout << "[System] Benchmark Complete!\n";
            cout << "-> Total Time for " << num_queries << " queries: " << total_duration.count() / 1000.0 << " ms\n";
            cout << "-> Average Latency: " << avg_latency_ms << " ms per query\n\n";
            continue;
        }

        if (user_query == "exit") break;
        if (user_query.empty()) continue;

        search(user_query);
    }

    return 0;
}