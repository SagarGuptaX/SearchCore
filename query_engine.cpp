#include "query_engine.h"
#include "index_store.h"
#include "tokenizer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <queue>
#include <chrono>
#include <filesystem>
using namespace std;
namespace fs = std::filesystem;

vector<int> intersect(const vector<Occurrence> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i].docID == list2[j].docID) { matched_docs.push_back(list1[i].docID); i++; j++; }
        else if (list1[i].docID < list2[j].docID) i++;
        else j++;
    }
    return matched_docs;
}

vector<int> intersect(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> matched_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i] == list2[j].docID) { matched_docs.push_back(list1[i]); i++; j++; }
        else if (list1[i] < list2[j].docID) i++;
        else j++;
    }
    return matched_docs;
}

vector<int> union_docs(const vector<int> &list1, const vector<Occurrence> &list2)
{
    vector<int> merged_docs;
    int i = 0, j = 0;
    while (i < list1.size() && j < list2.size())
    {
        if (list1[i] == list2[j].docID) { merged_docs.push_back(list1[i]); i++; j++; }
        else if (list1[i] < list2[j].docID) { merged_docs.push_back(list1[i]); i++; }
        else { merged_docs.push_back(list2[j].docID); j++; }
    }
    while (i < list1.size()) merged_docs.push_back(list1[i++]);
    while (j < list2.size()) merged_docs.push_back(list2[j++].docID);
    return merged_docs;
}

void filter_strict_phrase(
    vector<int> &matched_docs,
    const vector<string> &tokens,
    const unordered_map<string, vector<Occurrence>> &index,
    vector<pair<int, vector<pair<int, int>>>> &strict_page_hits)
{
    if (tokens.size() < 2) return;
    vector<int> strict_docs;

    for (int docID : matched_docs)
    {
        const Occurrence *first_occ = get_occurrence_by_doc(index.at(tokens[0]), docID);
        if (!first_occ) continue;

        vector<const Occurrence *> next_occ_list(tokens.size(), nullptr);
        bool missing_token = false;
        for (size_t i = 1; i < tokens.size(); i++)
        {
            if (!index.count(tokens[i])) { missing_token = true; break; }
            const Occurrence *next_occ = get_occurrence_by_doc(index.at(tokens[i]), docID);
            if (!next_occ) { missing_token = true; break; }
            next_occ_list[i] = next_occ;
        }
        if (missing_token) continue;

        bool strict_match_found = false;
        vector<pair<int, int>> doc_page_hits;

        for (const auto &[page, positions] : first_occ->page_positions)
        {
            int page_hits = 0;
            for (int start_pos : positions)
            {
                bool sequence_valid = true;
                for (size_t i = 1; i < tokens.size(); i++)
                {
                    if (!position_exists(next_occ_list[i], start_pos + i)) { sequence_valid = false; break; }
                }
                if (sequence_valid) { page_hits++; strict_match_found = true; }
            }
            if (page_hits > 0) doc_page_hits.push_back({page, page_hits});
        }
        if (strict_match_found)
        {
            strict_docs.push_back(docID);
            strict_page_hits.push_back({docID, move(doc_page_hits)});
        }
    }
    matched_docs = strict_docs;
}

double calculate_slop_multiplier(int docID, const vector<string> &tokens)
{
    if (tokens.size() < 2) return 1.0;
    double total_multiplier = 1.0;

    for (size_t i = 0; i < tokens.size() - 1; i++)
    {
        string word1 = tokens[i];
        string word2 = tokens[i + 1];
        if (!global_index.count(word1) || !global_index.count(word2)) continue;

        const Occurrence *occ1 = get_occurrence_by_doc(global_index.at(word1), docID);
        const Occurrence *occ2 = get_occurrence_by_doc(global_index.at(word2), docID);
        if (!occ1 || !occ2) continue;

        int min_distance = 999999;
        bool correct_direction = false;
        const auto &pages1 = occ1->page_positions;
        const auto &pages2 = occ2->page_positions;
        int pg1 = 0, p1_idx = 0, pg2 = 0, p2_idx = 0;

        while (pg1 < pages1.size() && pg2 < pages2.size())
        {
            int p1 = pages1[pg1].second[p1_idx];
            int p2 = pages2[pg2].second[p2_idx];
            int dist = abs(p2 - p1);
            if (dist < min_distance) { min_distance = dist; correct_direction = (p2 > p1); }

            if (p1 < p2)
            {
                p1_idx++;
                if (p1_idx >= pages1[pg1].second.size()) { pg1++; p1_idx = 0; }
            }
            else
            {
                p2_idx++;
                if (p2_idx >= pages2[pg2].second.size()) { pg2++; p2_idx = 0; }
            }
        }

        double pair_multiplier = 1.0;
        if (min_distance == 1) pair_multiplier = 2.0;
        else if (min_distance <= 5) pair_multiplier = 1.5;
        else if (min_distance <= 15) pair_multiplier = 1.2;
        if (pair_multiplier > 1.0 && correct_direction) pair_multiplier *= 1.2;
        total_multiplier *= pair_multiplier;
    }
    return total_multiplier;
}

void search(string query, bool silent)
{
    auto start_time = chrono::steady_clock::now();

    vector<string> tokens;
    bool is_or_query = false;
    if (query.length() >= 3 && tolower(query[0]) == 'o' && tolower(query[1]) == 'r' && query[2] == ':')
    { is_or_query = true; query = query.substr(3); }
    bool is_strict_query = false;
    if (query.length() >= 7 && query.substr(0, 7) == "strict:")
    { is_strict_query = true; query = query.substr(7); }

    std::replace(query.begin(), query.end(), '-', ' ');
    stringstream ss(query);
    string word;
    while (ss >> word)
    {
        word = clean_token(word);
        if (!word.empty()) tokens.push_back(word);
    }

    if (tokens.empty())
    {
        if (!silent) cout << "Query only contained stop-words (no meaningful words) or punctuation.\n";
        return;
    }
    if (tokens.size() > 10)
    {
        if (!silent) cout << "[WARNING] Query too long. Truncating to first 10 words.\n";
        tokens.resize(10);
    }

    vector<int> matched_docs;
    if (global_index.count(tokens[0]))
        for (auto &occ : global_index.at(tokens[0])) matched_docs.push_back(occ.docID);

    for (size_t i = 1; i < tokens.size(); i++)
    {
        if (matched_docs.empty() && !is_or_query) break;
        if (is_or_query)
        {
            if (global_index.count(tokens[i])) matched_docs = union_docs(matched_docs, global_index.at(tokens[i]));
        }
        else
        {
            if (global_index.count(tokens[i])) matched_docs = intersect(matched_docs, global_index.at(tokens[i]));
            else { matched_docs.clear(); break; }
        }
    }

    vector<pair<int, vector<pair<int, int>>>> strict_page_hits;
    if (is_strict_query) filter_strict_phrase(matched_docs, tokens, global_index, strict_page_hits);

    if (matched_docs.empty())
    {
        if (!silent) cout << "No matching documents found. Please ensure the typed spelling is correct.\n";
        return;
    }

    double avg_doc_len = 0.0;
    for (const int &x : doc_length) avg_doc_len += x;
    avg_doc_len /= (double)doc_length.size();

    double k1 = 1.2, b = 0.75, N_docs = doc_length.size();
    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> top_k_queue;
    int MAX_RESULTS = 25;

    for (int docID : matched_docs)
    {
        double total_score = 0.0;
        for (string q : tokens)
        {
            if (!global_index.count(q)) continue;
            double n_q = global_index.at(q).size();
            double idf = log((N_docs - n_q + 0.5) / (n_q + 0.5) + 1.0);
            double f_q_D = 0.0;
            const Occurrence *occ = get_occurrence_by_doc(global_index.at(q), docID);
            if (occ) f_q_D = occ->total_frequency;
            double dl = doc_length[docID];
            double numerator = f_q_D * (k1 + 1.0);
            double denominator = f_q_D + k1 * (1.0 - b + b * (dl / avg_doc_len));
            total_score += idf * (numerator / denominator);
        }
        double slop_multiplier = calculate_slop_multiplier(docID, tokens);
        total_score *= slop_multiplier;
        top_k_queue.push({total_score, docID});
        if (top_k_queue.size() > MAX_RESULTS) top_k_queue.pop();
    }

    vector<pair<double, int>> doc_scores;
    while (!top_k_queue.empty()) { doc_scores.push_back(top_k_queue.top()); top_k_queue.pop(); }
    std::reverse(doc_scores.begin(), doc_scores.end());

    auto end_time = chrono::steady_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time);
    if (silent) return;

    cout << "\n--- Top " << doc_scores.size() << " Search Results ---\n";
    int PAGE_SIZE = 10, current_index = 0;
    string user_input = "";

    while (current_index < doc_scores.size())
    {
        int end_index = min(current_index + PAGE_SIZE, (int)doc_scores.size());
        for (int i = current_index; i < end_index; i++)
        {
            int docID = doc_scores[i].second;
            double score = doc_scores[i].first;
            string file_path = ID_allocator[docID];
            vector<pair<int, int>> page_hits;

            if (is_strict_query && tokens.size() >= 2)
            {
                auto it = std::lower_bound(strict_page_hits.begin(), strict_page_hits.end(), docID,
                    [](const pair<int, vector<pair<int, int>>> &p, int val) { return p.first < val; });
                if (it != strict_page_hits.end() && it->first == docID) page_hits = it->second;
            }
            else
            {
                for (string q : tokens)
                {
                    if (!global_index.count(q)) continue;
                    const Occurrence *occ = get_occurrence_by_doc(global_index.at(q), docID);
                    if (occ)
                    {
                        for (auto const &[page, pos_list] : occ->page_positions)
                        {
                            auto ph_it = std::find_if(page_hits.begin(), page_hits.end(),
                                [page](const pair<int, int> &p) { return p.first == page; });
                            if (ph_it != page_hits.end()) ph_it->second += pos_list.size();
                            else page_hits.push_back({page, (int)pos_list.size()});
                        }
                    }
                }
            }

            int best_page = 999999, max_hits = 0;
            for (auto const &[page, hits] : page_hits)
                if (hits > max_hits || (hits == max_hits && page < best_page)) { max_hits = hits; best_page = page; }

            string filename = fs::path(file_path).filename().string();
            cout << "\n" << i + 1 << ". " << filename << " (Score: " << score << ")\n";
            if (best_page != 999999) cout << "   ↳ Best Match-> Page: " << best_page << "\n";

            int alternative_pages_printed = 0;
            bool has_alternatives = false;
            for (auto const &[page, hits] : page_hits)
            {
                if (page != best_page)
                {
                    if (!has_alternatives) { cout << "   ↳ Also mentioned on pages: "; has_alternatives = true; }
                    if (alternative_pages_printed < 5) { cout << (alternative_pages_printed > 0 ? ", " : "") << page; alternative_pages_printed++; }
                }
            }
            if (page_hits.size() > 6) cout << "...";
            if (has_alternatives) cout << "\n";
        }
        current_index += PAGE_SIZE;
        if (current_index < doc_scores.size())
        {
            cout << "\n[Press ENTER to show next 10 results, or type 'q' to start a new search] ";
            getline(cin, user_input);
            if (user_input == "q" || user_input == "Q") break;
        }
    }
    cout << "------------------------------\n";
    cout << "Search latency: " << duration.count() / 1000.0 << " ms\n\n";
}