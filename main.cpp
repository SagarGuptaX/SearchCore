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

using namespace std;
namespace fs = std::filesystem;
using dir_itr = std::filesystem::directory_iterator;

unordered_set<string> stop_words = {
    "website", "pfcindia", "co", "in", "cin", "l65910dl1986goi024862", "urjanidhi", "barakhamba", "connaught", "tel", "email",
    "hereby", "herein", "thereof", "thereto", "therein", "hereunder",
    "whereas", "pursuant", "aforesaid", "aforementioned", "hereinafter",
    "notwithstanding", "thereupon", "heretofore",
    "the", "a", "an", "this", "that", "these", "those", "each", "every", "all", "any",
    "of", "to", "in", "for", "on", "at", "by", "from", "with", "into", "about",
    "upon", "under", "over", "between", "through", "against", "during", "within",
    "and", "or", "but", "so", "if", "as", "than", "nor",
    "is", "are", "was", "were", "be", "been", "being",
    "has", "have", "had", "do", "did", "does", "will", "would",
    "may", "can", "shall", "should", "could", "might",
    "it", "its", "we", "our", "they", "their", "he", "she", "you", "i",
    "not", "no", "also", "such", "more", "other", "said", "up", "out", "then",
    "than", "however"};

// OPTIMIZATION: Flat Map for contiguous memory cache alignment
struct Occurrence
{
    int docID;
    int total_frequency;
    vector<pair<int, vector<int>>> page_positions;
};

unordered_map<string, vector<Occurrence>> global_index;
vector<string> ID_allocator;
vector<int> doc_length;
long long total_indx_words = 0;
long long total_pages = 0;

// HELPER: Binary Search to instantly find a docID in O(log N) instead of a slow for-loop
const Occurrence *get_occurrence_by_doc(const vector<Occurrence> &occ_list, int target_docID)
{
    auto it = std::lower_bound(occ_list.begin(), occ_list.end(), target_docID,
                               [](const Occurrence &o, int val)
                               { return o.docID < val; });
    if (it != occ_list.end() && it->docID == target_docID)
        return &(*it);
    return nullptr;
}

vector<int> intersect(const vector<Occurrence> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i].docID == list2[j].docID)
        {
            matched_docs.push_back(list1[i].docID);
            i++;
            j++;
        }
        else if (list1[i].docID < list2[j].docID)
            i++;
        else
            j++;
    }
    return matched_docs;
}

vector<int> intersect(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i] == list2[j].docID)
        {
            matched_docs.push_back(list1[i]);
            i++;
            j++;
        }
        else if (list1[i] < list2[j].docID)
            i++;
        else
            j++;
    }
    return matched_docs;
}

vector<int> union_docs(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> merged_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i] == list2[j].docID)
        {
            merged_docs.push_back(list1[i]);
            i++;
            j++;
        }
        else if (list1[i] < list2[j].docID)
        {
            merged_docs.push_back(list1[i]);
            i++;
        }
        else
        {
            merged_docs.push_back(list2[j].docID);
            j++;
        }
    }
    while (i < list1.size())
        merged_docs.push_back(list1[i++]);
    while (j < list2.size())
        merged_docs.push_back(list2[j++].docID);
    return merged_docs;
}

vector<int> filter_strict_phrase(const vector<int> &matched_docs, const vector<string> &tokens, const unordered_map<string, vector<Occurrence>> &index)
{
    if (tokens.size() < 2)
        return matched_docs;
    vector<int> strict_docs;

    for (int docID : matched_docs)
    {
        bool strict_match_found = false;

        const Occurrence *first_occ = get_occurrence_by_doc(index.at(tokens[0]), docID);
        if (!first_occ)
            continue;

        for (const auto &[page, positions] : first_occ->page_positions)
        {
            for (int start_pos : positions)
            {
                bool sequence_valid = true;

                for (size_t i = 1; i < tokens.size(); i++)
                {
                    if (!index.count(tokens[i]))
                    {
                        sequence_valid = false;
                        break;
                    }

                    const Occurrence *next_occ = get_occurrence_by_doc(index.at(tokens[i]), docID);
                    if (!next_occ)
                    {
                        sequence_valid = false;
                        break;
                    }

                    // Binary search the page
                    auto it = std::lower_bound(next_occ->page_positions.begin(), next_occ->page_positions.end(), page,
                                               [](const pair<int, vector<int>> &p, int val)
                                               { return p.first < val; });

                    if (it == next_occ->page_positions.end() || it->first != page)
                    {
                        sequence_valid = false;
                        break;
                    }

                    const auto &next_positions = it->second;
                    if (std::find(next_positions.begin(), next_positions.end(), start_pos + i) == next_positions.end())
                    {
                        sequence_valid = false;
                        break;
                    }
                }

                if (sequence_valid)
                {
                    strict_match_found = true;
                    break;
                }
            }
            if (strict_match_found)
                break;
        }

        if (strict_match_found)
        {
            strict_docs.push_back(docID);
        }
    }
    return strict_docs;
}

double calculate_slop_multiplier(int docID, const vector<string> &tokens)
{ // sagar: need to make a table of reward points per situation
    if (tokens.size() < 2)
        return 1.0;

    double total_multiplier = 1.0;

    for (size_t i = 0; i < tokens.size() - 1; i++)
    {
        string word1 = tokens[i];
        string word2 = tokens[i + 1]; // taking two words since we are claculating slop for two adjacent words

        if (!global_index.count(word1) || !global_index.count(word2))
            continue;

        const Occurrence *occ1 = get_occurrence_by_doc(global_index.at(word1), docID);
        const Occurrence *occ2 = get_occurrence_by_doc(global_index.at(word2), docID);

        if (!occ1 || !occ2)
            continue;

        int min_distance = 999999; // INT_MAX
        bool correct_direction = false;

        for (const auto &[page, pos_list1] : occ1->page_positions)
        {
            // Binary search the matching page
            auto it2 = std::lower_bound(occ2->page_positions.begin(), occ2->page_positions.end(), page,
                                        [](const pair<int, vector<int>> &p, int val)
                                        { return p.first < val; });

            if (it2 != occ2->page_positions.end() && it2->first == page)
            {
                const auto &pos_list2 = it2->second;

                // OPTIMIZATION: O(N+M) Two-Pointer approach instead of double loop
                int ptr1 = 0, ptr2 = 0;
                while (ptr1 < pos_list1.size() && ptr2 < pos_list2.size())
                {
                    int p1 = pos_list1[ptr1];
                    int p2 = pos_list2[ptr2];
                    int dist = abs(p2 - p1);

                    if (dist < min_distance)
                    {
                        min_distance = dist;
                        correct_direction = (p2 > p1);
                    }
                    if (p1 < p2)
                        ptr1++;
                    else
                        ptr2++;
                }
            }
        }

        double pair_multiplier = 1.0;
        if (min_distance == 1)
            pair_multiplier = 2.0;
        else if (min_distance <= 5)
            pair_multiplier = 1.5;
        else if (min_distance <= 15)
            pair_multiplier = 1.2;

        if (pair_multiplier > 1.0 && correct_direction)
        {
            pair_multiplier *= 1.2;
        }
        total_multiplier *= pair_multiplier;
    }

    return total_multiplier;
}

// ==========================================
// STAGE 3: SEARCH & BM25 RANKING// need to seperate bm25 ranking from search
// ==========================================
void search(string query, bool silent = false)
{
    auto start_time = chrono::steady_clock::now();

    vector<string> tokens;
    bool is_or_query = false;
    if (query.length() >= 3 && tolower(query[0]) == 'o' && tolower(query[1]) == 'r' && query[2] == ':')
    {
        is_or_query = true;
        query = query.substr(3);
    }
    bool is_strict_query = false;
    if (query.length() >= 7 && query.substr(0, 7) == "strict:")
    {
        is_strict_query = true;
        query = query.substr(7);
    }

    std::replace(query.begin(), query.end(), '-', ' ');
    stringstream ss(query);
    string word;

    while (ss >> word)
    {
        word.erase(std::remove_if(word.begin(), word.end(), [](unsigned char c)
                                  { return !std::isalnum(c); }),
                   word.end());
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        // IMPORTANT: Skip stop-words if it's a strict phrase so the distance gaps remain perfectly intact!
        if (stop_words.count(word))
            continue; // tokenisation will anyway remove stop words, no need to skip stop words in strict:

        // if (!is_strict_query && stop_words.count(word))
        //     continue;
        if (!word.empty())
            tokens.push_back(word);
    }

    if (tokens.empty())
    {
        if (!silent)
            std::cout << "Query only contained stop-words (no meaningful words) or punctuation.\n";
        return;
    }

    if (tokens.size() > 10)
    {
        if (!silent)
            std::cout << "[WARNING] Query too long. Truncating to first 10 words.\n";
        tokens.resize(10);
    } // need to mudularise code

    vector<int> matched_docs;

    if (global_index.count(tokens[0]))
    {
        for (auto &occ : global_index.at(tokens[0]))
        {
            matched_docs.push_back(occ.docID);
        }
    }

    for (size_t i = 1; i < tokens.size(); i++)
    {
        if (matched_docs.empty() && !is_or_query)
            break;

        if (is_or_query)
        {
            if (global_index.count(tokens[i]))
            {
                matched_docs = union_docs(matched_docs, global_index.at(tokens[i]));
            }
        }
        else
        {
            if (global_index.count(tokens[i]))
            {
                matched_docs = intersect(matched_docs, global_index.at(tokens[i]));
            }
            else
            {
                matched_docs.clear();
                break;
            }
        }
    }

    if (is_strict_query)
    {
        matched_docs = filter_strict_phrase(matched_docs, tokens, global_index);
    }

    if (matched_docs.empty())
    {
        if (!silent)
            cout << "No matching documents found. Please ensure the typed spelling is correct.\n";
        return;
    }

    // BM25 Math
    double avg_doc_len = 0.0;
    for (const int &x : doc_length)
        avg_doc_len += x;
    avg_doc_len /= (double)doc_length.size();

    double k1 = 1.2;
    double b = 0.75;
    double N_docs = doc_length.size();

    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> top_k_queue;
    int MAX_RESULTS = 25;

    for (int docID : matched_docs)
    {
        double total_score = 0.0;
        for (string q : tokens)
        {
            if (!global_index.count(q))
                continue;

            double n_q = global_index.at(q).size();
            double idf = log((N_docs - n_q + 0.5) / (n_q + 0.5) + 1.0);

            double f_q_D = 0.0;
            const Occurrence *occ = get_occurrence_by_doc(global_index.at(q), docID);
            if (occ)
                f_q_D = occ->total_frequency;

            double dl = doc_length[docID];
            double numerator = f_q_D * (k1 + 1.0);
            double denominator = f_q_D + k1 * (1.0 - b + b * (dl / avg_doc_len));
            total_score += idf * (numerator / denominator);
        }

        double slop_multiplier = calculate_slop_multiplier(docID, tokens);
        total_score *= slop_multiplier;

        top_k_queue.push({total_score, docID});
        if (top_k_queue.size() > MAX_RESULTS)
        {
            top_k_queue.pop();
        }
    }

    vector<pair<double, int>> doc_scores;
    while (!top_k_queue.empty())
    {
        doc_scores.push_back(top_k_queue.top());
        top_k_queue.pop();
    }
    std::reverse(doc_scores.begin(), doc_scores.end());

    auto end_time = chrono::steady_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time);

    if (silent)
        return;

    // 5. Pagination UI & Linker
    cout << "\n--- Top " << doc_scores.size() << " Search Results ---\n";

    int PAGE_SIZE = 10;
    int current_index = 0;
    string user_input = "";

    while (current_index < doc_scores.size())
    {
        int end_index = min(current_index + PAGE_SIZE, (int)doc_scores.size());

        for (int i = current_index; i < end_index; i++)
        {
            int docID = doc_scores[i].second;
            double score = doc_scores[i].first;
            string file_path = ID_allocator[docID];

            unordered_map<int, int> page_hits;
            // STRICT PAGE SCORING: Only reward pages that contain the exact phrase!
            if (is_strict_query && tokens.size() >= 2)
            {
                if (global_index.count(tokens[0]))
                {
                    const Occurrence *first_occ = get_occurrence_by_doc(global_index.at(tokens[0]), docID);
                    if (first_occ)
                    {
                        for (auto const &[page, positions] : first_occ->page_positions)
                        {
                            int strict_hits = 0;
                            for (int start_pos : positions)
                            {
                                bool sequence_valid = true;
                                for (size_t j = 1; j < tokens.size(); j++)
                                {
                                    if (!global_index.count(tokens[j]))
                                    {
                                        sequence_valid = false;
                                        break;
                                    }
                                    const Occurrence *next_occ = get_occurrence_by_doc(global_index.at(tokens[j]), docID);
                                    if (!next_occ)
                                    {
                                        sequence_valid = false;
                                        break;
                                    }

                                    auto it = std::lower_bound(next_occ->page_positions.begin(), next_occ->page_positions.end(), page,
                                                               [](const pair<int, vector<int>> &p, int val)
                                                               { return p.first < val; });

                                    if (it == next_occ->page_positions.end() || it->first != page)
                                    {
                                        sequence_valid = false;
                                        break;
                                    }

                                    const auto &next_positions = it->second;
                                    if (std::find(next_positions.begin(), next_positions.end(), start_pos + j) == next_positions.end())
                                    {
                                        sequence_valid = false;
                                        break;
                                    }
                                }
                                if (sequence_valid)
                                    strict_hits++;
                            }
                            if (strict_hits > 0)
                                page_hits[page] = strict_hits;
                        }
                    }
                }
            }
            // NORMAL PAGE SCORING
            else
            {
                for (string q : tokens)// maybe should let these ones handle within intersect and union too, calling unordeeep map again and agian can be heavy
                {
                    if (!global_index.count(q))
                        continue;
                    const Occurrence *occ = get_occurrence_by_doc(global_index.at(q), docID);
                    if (occ)
                    {
                        for (auto const &[page, pos_list] : occ->page_positions)
                        {
                            page_hits[page] += pos_list.size();
                        }
                    }
                }
            }

            int best_page = 999999;
            int max_hits = 0;

            // Simple loop to find best page and print the rest. Removes erasure overhead!
            for (auto const &[page, hits] : page_hits)
            {
                if (hits > max_hits || (hits == max_hits && page < best_page))
                {
                    max_hits = hits;
                    best_page = page;
                }
            }

            //why its removed and what us replacing all_pages.erase(std::remove(all_pages.begin(), all_pages.end(), best_page), all_pages.end()); // whats its doing? are we removing all after best page? what  the motto of this line?

            string filename = fs::path(file_path).filename().string();
            cout << "\n"
                 << i + 1 << ". " << filename << " (Score: " << score << ")\n";

            if (best_page != 999999)
                cout << "   ↳ Best Match-> Page: " << best_page << "\n";

            // Print alternative pages without array logic
            int alternative_pages_printed = 0;
            bool has_alternatives = false;
            for (auto const &[page, hits] : page_hits)
            {
                if (page != best_page)
                {
                    if (!has_alternatives)
                    {
                        cout << "   ↳ Also mentioned on pages: ";
                        has_alternatives = true;
                    }
                    if (alternative_pages_printed < 5)
                    {
                        cout << (alternative_pages_printed > 0 ? ", " : "") << page;
                        alternative_pages_printed++;
                    }
                }
            }
            if (page_hits.size() - 1 > 5)
                cout << "...";
            if (has_alternatives)
                cout << "\n";
        }

        current_index += PAGE_SIZE;

        if (current_index < doc_scores.size())
        {
            cout << "\n[Press ENTER to show next 10 results, or type 'q' to start a new search] ";
            getline(cin, user_input);
            if (user_input == "q" || user_input == "Q")
                break;
        }
    }
    cout << "------------------------------\n";
    cout << "Search latency: " << duration.count() / 1000.0 << " ms\n\n";
}

// stage 6
size_t get_corpus_hash(const string &corpus_path)
{
    try
    {
        std::chrono::system_clock::time_point latest_tp = std::chrono::system_clock::from_time_t(0);
        std::error_code ec;
        auto dir_time = fs::last_write_time(corpus_path, ec);
        if (!ec)
        {
            auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                dir_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            if (tp > latest_tp)
                latest_tp = tp;
        }

        for (const auto &entry : fs::directory_iterator(corpus_path, ec))
        {
            if (ec)
                break;
            std::error_code ec2;
            auto ftime = fs::last_write_time(entry.path(), ec2);
            if (ec2)
                continue;
            auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            if (tp > latest_tp)
                latest_tp = tp;
        }

        auto latest_time_t = std::chrono::system_clock::to_time_t(latest_tp);
        return static_cast<size_t>(latest_time_t);
    }
    catch (...)
    {
        return 0;
    }
}

// OPTIMIZATION: Flat Buffer Serialization - Writes one massive block instead of thousands of tiny disk calls!
bool save_index(const string &filename, size_t corpus_hash)
{
    ofstream out(filename, ios::binary);
    if (!out)
        return false;

    out.write((char *)&corpus_hash, sizeof(corpus_hash));
    out.write((char *)&total_pages, sizeof(total_pages));
    out.write((char *)&total_indx_words, sizeof(total_indx_words));

    size_t docs = ID_allocator.size();
    out.write((char *)&docs, sizeof(docs));

    for (const auto &name : ID_allocator)
    {
        size_t len = name.size();
        out.write((char *)&len, sizeof(len));
        out.write(name.data(), len);
    }

    size_t dl = doc_length.size();
    out.write((char *)&dl, sizeof(dl));
    out.write((char *)doc_length.data(), dl * sizeof(int));

    size_t words = global_index.size();
    out.write((char *)&words, sizeof(words));

    for (const auto &[word, occurrences] : global_index)
    {
        size_t wordLen = word.size();
        out.write((char *)&wordLen, sizeof(wordLen));
        out.write(word.data(), wordLen);

        // Flatten into a 1D int array buffer for single-call disk I/O
        vector<int> flat_buffer;
        flat_buffer.push_back(occurrences.size());

        for (const auto &occ : occurrences)
        {
            flat_buffer.push_back(occ.docID);
            flat_buffer.push_back(occ.total_frequency);
            flat_buffer.push_back(occ.page_positions.size());

            for (const auto &[page, positions] : occ.page_positions)
            {
                flat_buffer.push_back(page);
                flat_buffer.push_back(positions.size());
                for (int pos : positions)
                {
                    flat_buffer.push_back(pos);
                }
            }
        }

        size_t buf_size = flat_buffer.size();
        out.write((char *)&buf_size, sizeof(buf_size));
        out.write((char *)flat_buffer.data(), buf_size * sizeof(int));
    }

    return true;
}

bool load_index(const string &filename)
{
    ifstream in(filename, ios::binary);
    if (!in)
        return false;

    size_t dummy_hash;
    in.read((char *)&dummy_hash, sizeof(dummy_hash));

    global_index.clear();
    ID_allocator.clear();
    doc_length.clear();

    in.read((char *)&total_pages, sizeof(total_pages));
    in.read((char *)&total_indx_words, sizeof(total_indx_words));

    size_t docs;
    in.read((char *)&docs, sizeof(docs));

    ID_allocator.resize(docs);
    for (size_t i = 0; i < docs; i++)
    {
        size_t len;
        in.read((char *)&len, sizeof(len));
        ID_allocator[i].resize(len);
        in.read(&ID_allocator[i][0], len);
    }

    size_t dl;
    in.read((char *)&dl, sizeof(dl));

    doc_length.resize(dl);
    in.read((char *)doc_length.data(), dl * sizeof(int));

    size_t words;
    in.read((char *)&words, sizeof(words));

    for (size_t i = 0; i < words; i++)
    {
        size_t wordLen;
        in.read((char *)&wordLen, sizeof(wordLen));
        string word(wordLen, '\0');
        in.read(&word[0], wordLen);

        size_t buf_size;
        in.read((char *)&buf_size, sizeof(buf_size));

        vector<int> flat_buffer(buf_size);
        in.read((char *)flat_buffer.data(), buf_size * sizeof(int));

        int ptr = 0;
        int occCount = flat_buffer[ptr++];
        vector<Occurrence> occs(occCount);

        for (int j = 0; j < occCount; j++)
        {
            occs[j].docID = flat_buffer[ptr++];
            occs[j].total_frequency = flat_buffer[ptr++];

            int pageCount = flat_buffer[ptr++];
            for (int k = 0; k < pageCount; k++)
            {
                int page = flat_buffer[ptr++];
                int posCount = flat_buffer[ptr++];

                vector<int> positions(posCount);
                for (int p = 0; p < posCount; p++)
                {
                    positions[p] = flat_buffer[ptr++];
                }
                occs[j].page_positions.push_back({page, move(positions)});
            }
        }
        global_index[word] = move(occs);
    }

    return true;
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
                cout << "\n-> Successfully loaded " << doc_length.size() << " documents from cache in " << load_dur.count() / 1000.0 << " ms.\n"
                     << endl;
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

        for (const auto &file : dir_itr(corpus_path))
        {
            int fileID = ID_allocator.size();
            ID_allocator.push_back(file.path().string());

            std::ifstream in_file(file.path().string());

            if (in_file.is_open())
            {
                int total_words = 0;
                int current_page_number = 1;
                int absolute_word_position = 0;

                string line;
                while (getline(in_file, line))
                {
                    if (line.find('\f') != string::npos)
                    {
                        current_page_number++;
                        total_pages++;
                        absolute_word_position = 0;
                        line.erase(std::remove(line.begin(), line.end(), '\f'), line.end());
                    }

                    std::replace(line.begin(), line.end(), '-', ' ');
                    stringstream line_ss(line);
                    string raw_word;

                    while (line_ss >> raw_word)
                    {
                        raw_word.erase(std::remove_if(raw_word.begin(), raw_word.end(), [](unsigned char c)
                                                      { return !std::isalnum(c); }),
                                       raw_word.end());
                        std::transform(raw_word.begin(), raw_word.end(), raw_word.begin(), [](unsigned char c)
                                       { return std::tolower(c); });

                        if (stop_words.count(raw_word))
                            continue;

                        if (!raw_word.empty())
                        {
                            total_indx_words++;
                            absolute_word_position++;
                            total_words++;

                            // OPTIMIZATION: Write directly to global_index! Bypasses slow temp hash map allocation.
                            auto &word_occs = global_index[raw_word];
                            if (word_occs.empty() || word_occs.back().docID != fileID)
                            {
                                word_occs.push_back({fileID, 0, {}});
                            }

                            Occurrence &current_occ = word_occs.back();
                            current_occ.total_frequency++;

                            auto &pages = current_occ.page_positions;
                            if (pages.empty() || pages.back().first != current_page_number)
                            {
                                pages.push_back({current_page_number, {absolute_word_position}});
                            }
                            else
                            {
                                pages.back().second.push_back(absolute_word_position);
                            }
                        }
                    }
                }
                doc_length.push_back(total_words);
            }
        }

        auto build_end = chrono::steady_clock::now();
        auto build_dur = chrono::duration_cast<chrono::microseconds>(build_end - build_start);
        cout << "\n-> Successfully build Index of  " << doc_length.size() << " documents in " << build_dur.count() / 1000.0 << " ms from scratch.\n";
        cout << "\n[System] Parsing complete. Serializing index to disk...\n";

        auto saved_start = chrono::steady_clock::now();
        save_index(index_file, get_corpus_hash(corpus_path));
        auto saved_end = chrono::steady_clock::now();
        auto saved_dur = chrono::duration_cast<chrono::microseconds>(saved_end - saved_start);
        cout << "\n-> Successfully Serialized Index of " << doc_length.size() << " documents in " << saved_dur.count()/1000.0 << " ms from scratch.\n";
    }

    double mb = fs::file_size(index_file) / (1024.0 * 1024.0);

    cout << "Index successfully built! Loaded " << doc_length.size() << " documents.\n\n";
    cout << fixed << setprecision(2) << "Corpus size: " << mb << " MiB\n\n";
    cout << "Total indexed tokens: " << total_indx_words << "\n\n";
    cout << "Total pages: " << total_pages << "\n\n";
    cout << "Total unique tokens: " << global_index.size() << "\n\n";

    cout << "Welcome User! SearchCore is a file search engine, supporting AND/OR boolean logic.\nBY default all search queries are case insensitive and will be processed in AND logic.\nTo use OR logic, use 'or:' in the beggining of your search query.\nTo use Exact Phrase Search, use 'strict:' in the beggining of your search query.\n\n";

    string user_query;
    while (true)
    {
        cout << "Enter search query (or type 'exit' to quit): ";
        getline(cin, user_query);

        if (user_query == "!benchmark")
        {
            cout << "\n[System] Initiating 1,000-query benchmark test...\n\n";
            vector<string> all_words;
            for (const auto &[word, occ] : global_index)
                all_words.push_back(word);
            if (all_words.empty())
                continue;

            auto total_start = chrono::steady_clock::now();

            for (int i = 0; i < 200; i++)
            {
                string random_q = all_words[rand() % all_words.size()];
                search(random_q, true);
            }
            for (int i = 0; i < 600; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()];
                search(random_q, true);
            }
            for (int i = 0; i < 200; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()] + " " + all_words[rand() % all_words.size()];
                search(random_q, true);
            }

            auto total_end = chrono::steady_clock::now();
            auto total_duration = chrono::duration_cast<chrono::microseconds>(total_end - total_start);
            double avg_latency_ms = (total_duration.count() / 1000.0) / 1000;

            cout << "[System] Benchmark Complete!\n";
            cout << "-> Total Time for 1000 queries: " << total_duration.count() / 1000.0 << " ms\n";
            cout << "-> Average Latency: " << avg_latency_ms << " ms per query\n\n";
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