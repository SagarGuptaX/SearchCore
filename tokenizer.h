#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <unordered_set>

extern std::unordered_set<std::string> stop_words;

std::string clean_token(std::string raw_word);

#endif