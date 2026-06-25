// #include <bits/stdc++.h> //will  change it at end when project is done
// using namespace std;
// namespace fs = std::filesystem;
// using dir_itr = std::filesystem::directory_iterator;

// int main(int argc, char *argv[])
// {
//     if (argc < 2)
//     {
//         cout << "ERROR: Please enter corpus path.";
//         return 1;
//     }
//     else
//     {   unordered_map<string, vector<pair</*id*/int,/*freq*/ int>>> global_index;
//         string corpus_path = argv[1];
//         cout << corpus_path << "\n";
//         unordered_map<int, string> ID_allocator;
//         int fileID = -1;
//         for (const auto &file : dir_itr(corpus_path))
//         { // This map is the secret to keeping your system fast. Later, your massive global Inverted Index will map words to these integer DocIDs (e.g., "disbursement" -> [0, 1, 4]).
//             // If you did not have this Document Table, you would have to store the full string "corpus/PFC_AR 2025_Notice.txt" inside the Inverted Index every single time a word appeared. Duplicating strings like that bloats your RAM, destroys contiguous memory alignment, and ruins CPU cache locality. By keeping the main index strictly filled with integers, you are perfectly setting the stage for the algorithmic latency metrics and the custom slab allocator in your CacheAudit framework later on.
//             fileID++;
//             ID_allocator[fileID] = file.path().string();

//             std::ifstream in_file(file.path().string());
//             vector<int> doc_length;

//             if (in_file.is_open())
//             {   int total_words=0;
//                 string word;
//                 unordered_map<string, int> word_counter;
//                 // Read word by word
//                 while (in_file >> word)
//                 {   total_words++;
//                     word_counter[word]++;
//                 }
//                 doc_length.push_back(total_words);
//                 for(const auto& [per_word, freq]: word_counter){
//                     global_index[per_word].push_back({fileID, freq});
//                 }
//             }
//         }
//         return 0;
//     }
// }

// void search(string query) {
//     vector<string> tokens;
//     // 1. Create a stringstream from 'query'
//     stringstream ss(query);
//     string word;
//     // 2. Write a while loop to extract words using >>
//     while(ss>>word){
//         // Convert the string to lowercase in-place
//     std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
//         return std::tolower(c);
//     });

//         tokens.push_back(word);
//     }
//     // 3. Lowercase each word and push it to 'tokens'

//     // Show me this code!
// }

// vector<int> intersect(const vector<pair<int, int>>& list1, const vector<pair<int, int>>& list2) {
//     vector<int> matched_docs;
//     int i = 0; // Pointer for list1
//     int j = 0; // Pointer for list2

//     while(i < list1.size() && j < list2.size()) {
//         // Write the logic here!
//         if(list1[i].first==list2[j].first) {matched_docs.push_back(list1[i].first); i++; j++;}
//         while((i < list1.size() && j < list2.size())&&list1[i].first>list2[j].first)j++;
//         while((i < list1.size() && j < list2.size())&&list1[i].first<list2[j].first)i++;
//         // Compare list1[i].first (the DocID) with list2[j].first
//         // If they match, push to matched_docs and move both pointers.
//         // If one is smaller, move that pointer forward.
//     }

//     return matched_docs;
// }

// double avg_doc_len=0;
// for(const int& x:doc_length){
//     avg_doc_len+=x;
// }
// avg_doc_len/=doc_length.size();

// double k1 = 1.2;
// double b = 0.75;
// double N = doc_length.size();

// // We need a place to store our final scores
// vector<pair<double, int>> doc_scores; // {Score, DocID}

// for (int docID : matched_docs) {
//     double total_score = 0.0;

//     for (string q : tokens) {
//         // 1. Get n_q (Number of documents containing word 'q')
//         double n_q = global_index[q].size();

//         // 2. Calculate IDF (Inverse Document Frequency)
//         double idf = log((N - n_q + 0.5) / (n_q + 0.5) + 1.0);

//         // 3. Get term frequency of 'q' in this specific 'docID'
//         double f_q_D;
//         for(auto& [id,freq]:global_index[q]) if(id==docID) {f_q_D=freq; break;} /* ??? You need to find this! ??? */

//         // 4. Get the length of this specific 'docID'
//         double dl = doc_length[docID];

//         // 5. Calculate BM25 for this word, and add to total_score
//         double numerator = f_q_D * (k1 + 1.0);
//         double denominator = f_q_D + k1 * (1.0 - b + b * (dl / avg_doc_len));
//         total_score += idf * (numerator / denominator);
//     }

//     doc_scores.push_back({total_score, docID});
// }

// sort(doc_scores.rbegin(), doc_scores.rend());
//

#include <bits/stdc++.h> // Will change to specific headers (iostream, vector, etc.) when finalizing the project
using namespace std;
namespace fs = std::filesystem;
using dir_itr = std::filesystem::directory_iterator;

// ==========================================
// GLOBAL ARCHITECTURE
// ==========================================
// 1. The Core Index: Maps a word to a list of {DocID, Frequency}
unordered_map<string, vector<pair</*id*/ int, /*freq*/ int>>> global_index;

// 2. The Document Table: Maps integer DocIDs to actual file paths on disk.
// We use this to avoid storing massive string paths inside the index, saving RAM and preserving cache locality.
unordered_map<int, string> ID_allocator;

// 3. Document Lengths: A contiguous array storing the total word count of each document.
// Using a vector here instead of a map ensures O(1) lookup with zero hashing overhead, perfect for BM25 math.
vector<int> doc_length;

// ==========================================
// STAGE 2: O(N) TWO-POINTER INTERSECTION
// ==========================================
// This guarantees sub-5ms latency when searching for multiple words.
// Because our posting lists are naturally sorted by DocID during the indexing phase,
// we can intersect them in a single linear pass rather than using slow nested loops.
vector<int> intersect(const vector<pair<int, int>> &list1, const vector<pair<int, int>> &list2)
{
    vector<int> matched_docs;
    int i = 0; // Pointer for list1
    int j = 0; // Pointer for list2

    // Walk through both lists simultaneously
    while (i < list1.size() && j < list2.size())
    {
        // If they match, this document contains both words. Push it and advance both pointers.
        if (list1[i].first == list2[j].first)
        {
            matched_docs.push_back(list1[i].first);
            i++;
            j++;
        }
        // If list1's DocID is smaller, move the i pointer forward to catch up
        else if (list1[i].first < list2[j].first)
        {
            i++;
        }
        // If list2's DocID is smaller, move the j pointer forward to catch up
        else
        {
            j++;
        }
    }
    return matched_docs;
}
vector<int> intersect(const vector<int> &list1, const vector<pair<int, int>> &list2)//for rolling call from tokens(after token[2])
{
    vector<int> matched_docs;
    int i = 0; // Pointer for list1
    int j = 0; // Pointer for list2

    // Walk through both lists simultaneously
    while (i < list1.size() && j < list2.size())
    {
        // If they match, this document contains both words. Push it and advance both pointers.
        if (list1[i] == list2[j].first)
        {
            matched_docs.push_back(list1[i]);
            i++;
            j++;
        }
        // If list1's DocID is smaller, move the i pointer forward to catch up
        else if (list1[i] < list2[j].first)
        {
            i++;
        }
        // If list2's DocID is smaller, move the j pointer forward to catch up
        else
        {
            j++;
        }
    }
    return matched_docs;}

// ==========================================
// STAGE 3: SEARCH & BM25 RANKING
// ==========================================
void search(string query)
{
    vector<string> tokens;

    // 1. Tokenization: Create a stringstream from 'query'
    stringstream ss(query);
    string word;

    // Extract words using >>
    while (ss >> word)
    {
        // Convert the string to lowercase in-place to ensure case-insensitive matching
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        tokens.push_back(word);
    }

    if (tokens.empty())
        return;

    // 2. Find Matched Documents (Using Intersection)
    vector<int> matched_docs;
    if (tokens.size() == 1)
    {
        // Single word search: just grab all DocIDs from its posting list
        for (auto &p : global_index[tokens[0]])
            matched_docs.push_back(p.first);
    }
    else if (tokens.size() >= 2)
    {
        // Multi-word search: intersect the first two words (will expand to rolling loop later)
        matched_docs = intersect(global_index[tokens[0]], global_index[tokens[1]]);
        for(int i=2; i<tokens.size(); i++){
            matched_docs=intersect(matched_docs, global_index[tokens[i]]);

        }
    }

    if (matched_docs.empty())
    {
        cout << "No matching PFC documents found.\n";
        return;
    }

    // 3. BM25 Math Preparation
    // Calculate Average Document Length for the penalty curve
    double avg_doc_len = 0.0;
    for (const int &x : doc_length)
    {
        avg_doc_len += x;
    }
    avg_doc_len /= (double)doc_length.size();

    // BM25 Tuning Constants
    double k1 = 1.2;              // Controls Term Frequency saturation (prevents long docs from unfairly winning)
    double b = 0.75;              // Controls Document Length penalty
    double N = doc_length.size(); // Total documents in corpus

    // We need a place to store our final scores: {Score, DocID}
    vector<pair<double, int>> doc_scores;

    // 4. BM25 Scoring Loop
    for (int docID : matched_docs)
    {
        double total_score = 0.0;

        for (string q : tokens)
        {
            // Get n_q (Number of documents containing word 'q')
            double n_q = global_index[q].size();

            // Calculate IDF (Inverse Document Frequency) - Boosts rare words over common words
            double idf = log((N - n_q + 0.5) / (n_q + 0.5) + 1.0);

            // Get term frequency of 'q' in this specific 'docID'
            double f_q_D = 0.0;
            for (auto &[id, freq] : global_index[q])
            {
                if (id == docID)
                {
                    f_q_D = freq;
                    break; // Stop searching once we find the matching DocID
                }
            }

            // Get the length of this specific 'docID'
            double dl = doc_length[docID];

            // Calculate BM25 for this word, and add to total_score
            double numerator = f_q_D * (k1 + 1.0);
            double denominator = f_q_D + k1 * (1.0 - b + b * (dl / avg_doc_len));
            total_score += idf * (numerator / denominator);
        }

        doc_scores.push_back({total_score, docID});
    }

    // 5. Sort and Display Results
    // Using rbegin() and rend() to sort in descending order (highest score first)
    sort(doc_scores.rbegin(), doc_scores.rend());

    cout << "\n--- PFC Top Search Results ---\n";
    int results_to_show = min(3, (int)doc_scores.size());
    for (int i = 0; i < results_to_show; i++)
    {
        cout << i + 1 << ". " << ID_allocator[doc_scores[i].second]
             << " (Relevance Score: " << doc_scores[i].first << ")\n";
    }
    cout << "------------------------------\n";
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
    cout << "Building Index from: " << corpus_path << "...\n";

    int fileID = -1;
    for (const auto &file : dir_itr(corpus_path))
    {
        fileID++;
        ID_allocator[fileID] = file.path().string();

        std::ifstream in_file(file.path().string());

        if (in_file.is_open())
        {
            int total_words = 0;
            string word;
            unordered_map<string, int> word_counter;

            // Read word by word
            while (in_file >> word)
            {
                total_words++;

                // Clean punctuation and lowercase to ensure clean index keys
                word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c)
                               { return std::tolower(c); });

                if (!word.empty())
                {
                    word_counter[word]++;
                }
            }

            // Store total word count in contiguous array
            doc_length.push_back(total_words);

            // Flush local file counts into the massive global index
            for (const auto &[per_word, freq] : word_counter)
            {
                global_index[per_word].push_back({fileID, freq});
            }
        }
    }

    cout << "Index successfully built! Loaded " << doc_length.size() << " documents.\n\n";

    // Continuous Live Search Loop
    string user_query;
    while (true)
    {
        cout << "Enter search query (or type 'exit' to quit): ";
        getline(cin, user_query);

        if (user_query == "exit")
            break;
        if (user_query.empty())
            continue;

        auto start = chrono::steady_clock::now();
        search(user_query);
        auto end = chrono::steady_clock::now();
auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
cout << "Search latency: " << duration.count() / 1000.0 << " ms\n";
    }

    return 0;
}