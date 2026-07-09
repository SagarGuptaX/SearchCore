#pragma once
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

struct Occurrence
{
    int docID;
    int total_frequency;
    std::vector<std::pair<int, std::vector<int>>> page_positions;
};

extern std::unordered_map<std::string, std::vector<Occurrence>> global_index;
extern std::vector<std::string> ID_allocator;
extern std::vector<int> doc_length;
extern long long total_indx_words;
extern long long total_pages;

const Occurrence* get_occurrence_by_doc(const std::vector<Occurrence>& occ_list, int target_docID);
bool position_exists(const Occurrence* occ, int target_pos);
void build_index_from_corpus(const std::string &corpus_path);