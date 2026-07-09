#include "tokenizer.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
using namespace std;

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

std::string clean_token(std::string raw_word)
{
    raw_word.erase(std::remove_if(raw_word.begin(), raw_word.end(),
                                  [](unsigned char c)
                                  { return !std::isalnum(c); }),
                   raw_word.end());
    std::transform(raw_word.begin(), raw_word.end(), raw_word.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
                   if(stop_words.count(raw_word)) raw_word="";
    return raw_word;
}
