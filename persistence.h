#pragma once
#include <string>

size_t get_corpus_hash(const std::string &corpus_path);
bool save_index(const std::string &filename, size_t corpus_hash);
bool load_index(const std::string &filename);