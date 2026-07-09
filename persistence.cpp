#include "persistence.h"
#include "index_store.h"
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace std;
namespace fs = std::filesystem;

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
            if (tp > latest_tp) latest_tp = tp;
        }
        for (const auto &entry : fs::directory_iterator(corpus_path, ec))
        {
            if (ec) break;
            std::error_code ec2;
            auto ftime = fs::last_write_time(entry.path(), ec2);
            if (ec2) continue;
            auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            if (tp > latest_tp) latest_tp = tp;
        }
        auto latest_time_t = std::chrono::system_clock::to_time_t(latest_tp);
        return static_cast<size_t>(latest_time_t);
    }
    catch (...) { return 0; }
}

bool save_index(const string &filename, size_t corpus_hash)
{
    ofstream out(filename, ios::binary);
    if (!out) return false;

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
                for (int pos : positions) flat_buffer.push_back(pos);
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
    if (!in) return false;

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
                for (int p = 0; p < posCount; p++) positions[p] = flat_buffer[ptr++];
                occs[j].page_positions.push_back({page, move(positions)});
            }
        }
        global_index[word] = move(occs);
    }
    return true;
}