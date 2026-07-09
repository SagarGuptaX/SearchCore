#include "index_store.h"
#include "tokenizer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace std;

namespace fs = std::filesystem;
using dir_itr = std::filesystem::directory_iterator;

unordered_map<string, vector<Occurrence>> global_index;
vector<string> ID_allocator;
vector<int> doc_length;
long long total_indx_words = 0;
long long total_pages = 0;

const Occurrence *get_occurrence_by_doc(const vector<Occurrence> &occ_list, int target_docID)
{
    auto it = std::lower_bound(occ_list.begin(), occ_list.end(), target_docID,
                               [](const Occurrence &o, int val)
                               { return o.docID < val; });
    if (it != occ_list.end() && it->docID == target_docID)
        return &(*it);
    return nullptr;
}

bool position_exists(const Occurrence *occ, int target_pos)
{
    for (const auto &[page, positions] : occ->page_positions)
    {
        if (positions.empty())
            continue;
        if (target_pos >= positions.front() && target_pos <= positions.back())
        {
            return std::binary_search(positions.begin(), positions.end(), target_pos);
        }
    }
    return false;
}

void build_index_from_corpus(const std::string &corpus_path)
{
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
                    line.erase(std::remove(line.begin(), line.end(), '\f'), line.end());
                }
                std::replace(line.begin(), line.end(), '-', ' ');
                stringstream line_ss(line);
                string raw_word;

                while (line_ss >> raw_word)
                {
                    raw_word = clean_token(raw_word);
                    if (!raw_word.empty())
                    {
                        total_indx_words++;
                        absolute_word_position++;
                        total_words++;
                        auto &word_occs = global_index[raw_word];
                        if (word_occs.empty() || word_occs.back().docID != fileID)
                            word_occs.push_back({fileID, 0, {}});
                        Occurrence &current_occ = word_occs.back();
                        current_occ.total_frequency++;
                        auto &pages = current_occ.page_positions;
                        if (pages.empty() || pages.back().first != current_page_number)
                            pages.push_back({current_page_number, {absolute_word_position}});
                        else
                            pages.back().second.push_back(absolute_word_position);
                    }
                }
            }
            doc_length.push_back(total_words);
        }
    }
}