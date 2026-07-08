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

unordered_set<string> stop_words = {//stop words
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

struct Occurrence
{
    int docID;
    int total_frequency;
    unordered_map<int, vector<int>> page_positions;// this can be made into vector of vectors, since docid are in order
};

unordered_map<string, vector<Occurrence>> global_index;
vector<string> ID_allocator;
vector<int> doc_length;
long long total_indx_words = 0;
long long total_pages = 0;

vector<int> intersect(const vector<Occurrence> &list1, const vector<Occurrence> &list2)//AND
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

        // Grab the positional data for the first word
        const Occurrence *first_occ = nullptr;
        for (const auto &o : index.at(tokens[0]))//why from token[0]? wouldnt it be already processed in intersect? shouldnt we stat from indx 1?
        {
            if (o.docID == docID)
            {
                first_occ = &o;
                break;
            }
        }
        if (!first_occ)
            continue;

        // Check every page where the first word appears
        for (const auto &[page, positions] : first_occ->page_positions)
        {
            for (int start_pos : positions)
            {
                bool sequence_valid = true;

                // Check if subsequent words immediately follow
                for (size_t i = 1; i < tokens.size(); i++)
                {
                    const Occurrence *next_occ = nullptr;
                    for (const auto &o : index.at(tokens[i]))
                    {
                        if (o.docID == docID)
                        {
                            next_occ = &o;
                            break;
                        }
                    }

                    if (!next_occ || !next_occ->page_positions.count(page))//||!next_occ->page_positions.count(page+1))// i added the third boolean
                    {
                        sequence_valid = false;
                        break;
                    }

                    // Look for the exact expected position (start_pos + i)
                    const auto &next_positions = next_occ->page_positions.at(page);
                    if (std::find(next_positions.begin(), next_positions.end(), start_pos + i) == next_positions.end())
                    {
                        sequence_valid = false;
                        break;
                    }
                    // if(next_occ && !next_occ->page_positions.count(page)&&next_occ->page_positions.count(page+1)){
                    //     const auto &next_positions = next_occ->page_positions.at(page+1);
                    // }
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
        return 1.0;//

    double total_multiplier = 1.0;

    for (size_t i = 0; i < tokens.size() - 1; i++)
    {
        string word1 = tokens[i];
        string word2 = tokens[i + 1]; // taking two words since we are claculating slop for two adjacent words

        // Memory Safety: Do not insert missing words into global_index
        if (!global_index.count(word1) || !global_index.count(word2))
            continue;

        const Occurrence *occ1 = nullptr;
        const Occurrence *occ2 = nullptr;

        for (const auto &o : global_index.at(word1))
        {
            if (o.docID == docID)
            {
                occ1 = &o;
                break;
            }
        }
        for (const auto &o : global_index.at(word2))
        {
            if (o.docID == docID)
            {
                occ2 = &o;
                break;
            }
        } 

        if (!occ1 || !occ2)
            continue;

        int min_distance = 999999; // INT_MAX
        bool correct_direction = false;

        for (const auto &[page, pos_list1] : occ1->page_positions)
        {
            if (occ2->page_positions.count(page))
            {
                const auto &pos_list2 = occ2->page_positions.at(page);
                for (int p1 : pos_list1)
                {
                    for (int p2 : pos_list2)// positions are  incremental, we can use two pointer here
                    {
                        int dist = abs(p2 - p1); // no way to reduce this double loop?? the whole code seems robust yet inefficeint.
                        if (dist < min_distance)
                        {
                            min_distance = dist;
                            correct_direction = (p2 > p1);
                        }
                    }
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
    if (query.length() >= 3 &&
        tolower(query[0]) == 'o' &&
        tolower(query[1]) == 'r' &&
        query[2] == ':')
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
            continue;

        if (!word.empty())
        {
            tokens.push_back(word);
        }
    }

    if (tokens.empty())
    {
        if (!silent)
            std::cout << "Query only contained stop-words (no meaningful words) or punctuation." << endl;
        return;
    }

    if (tokens.size() > 10)
    { // need to understand use of silent
        if (!silent)
            std::cout << "[WARNING] Query too long. Truncating to first 10 words." << endl;
        tokens.resize(10);
    } // need to mudularise code

    vector<int> matched_docs;

    // Safely load the first word's documents using .count() and .at()
    if (global_index.count(tokens[0]))
    { // there are too many checks to avoid error,
        // question 1, logically is it even possible for some checks to get bypassed gien they werent present,
        // question two, how can one know which chcek failed and made code skip some critical lines/fucntion
        for (auto &occ : global_index.at(tokens[0]))
        {
            matched_docs.push_back(occ.docID);// there seems no use of first intersect
        }
    }

    for (size_t i = 1; i < tokens.size(); i++)
    {
        if (matched_docs.empty() && !is_or_query)
            break; // matche_empty seems unnecsary check since , no its nice, if token[0] dindt exist in corpus its instant exit for AND

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
                matched_docs.clear(); // Word doesn't exist, AND query instantly fails
                break;
            }
        }

        // If it's a strict query, filter the matched_docs by absolute positions!
    } // whats the overhaead or say load of the function that calculates the hyte differenec of the corpus, and how it knows the last size of corpus??
    if (is_strict_query)
    {
        matched_docs = filter_strict_phrase(matched_docs, tokens, global_index);
    }
    if (matched_docs.empty())
    {
        if (!silent)
            cout << "No matching documents found. Please ensure the typed spelling is correct." << endl;
        return;
    }
//bm25
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
                continue; // Memory safety// sagar: its understandable here, since tokesn is user input just parsed

            double n_q = global_index.at(q).size();
            double idf = log((N_docs - n_q + 0.5) / (n_q + 0.5) + 1.0);

            double f_q_D = 0.0;
            for (auto &occ : global_index.at(q))
            {
                if (occ.docID == docID)
                {
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

    // [SHORT CIRCUIT FOR BENCHMARKS]
    if (silent)
    {
        return;
    }

    // 5. Pagination UI & Linker
    cout << "\n--- Top " << doc_scores.size() << " Search Results ---\n"
         << endl;

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
                    const Occurrence *first_occ = nullptr;
                    for (const auto &o : global_index.at(tokens[0]))// this one is repeated many times in the code, it should be able to reduce redundancy
                    {
                        if (o.docID == docID)
                        {
                            first_occ = &o;
                            break;
                        }
                    }

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
                                    const Occurrence *next_occ = nullptr;
                                    for (const auto &o : global_index.at(tokens[j]))
                                    {
                                        if (o.docID == docID)
                                        {
                                            next_occ = &o;
                                            break;
                                        }
                                    }

                                    if (!next_occ || !next_occ->page_positions.count(page))
                                    {
                                        sequence_valid = false;
                                        break;
                                    }

                                    const auto &next_positions = next_occ->page_positions.at(page);
                                    if (std::find(next_positions.begin(), next_positions.end(), start_pos + j) == next_positions.end())
                                    {
                                        sequence_valid = false;
                                        break;
                                    }
                                }
                                if (sequence_valid)
                                    strict_hits++;//..yes its redundant, i should get hit score of strict from filter strict() itself.
                            }
                            if (strict_hits > 0)
                            {
                                page_hits[page] = strict_hits;
                            }
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
                    for (auto &occ : global_index.at(q))
                    {
                        if (occ.docID == docID)
                        {
                            for (auto const &[page, pos_list] : occ.page_positions)
                            {
                                page_hits[page] += pos_list.size();// not really sure what  thast doing// i shouldnt use map here
                            }//for cross page strict/intersect  checking, absolute word must not be zero, for token[0], we will find docid/ first page, etc and from there will be able toch ifvwords are like pos, pos+1, pos+2, etc,
                            //actually intersect/union arent related to this, its about slop calculator
                            break;
                        }
                    }
                }
            }

            int best_page = 999999;
            int max_hits = -1; // illogical; why not 0? i dont want 0 hit page, seems useless
            vector<int> all_pages;

            for (auto const &[page, hits] : page_hits)// this whole loop seems redundant, it could be fitted in the above [page,pos_list]
            { // this seems redundant, why to make map for this, why not do it directly? a possible explanation can be that
                //[page] are repeative//they shouldnt  be repeative
                all_pages.push_back(page);
                if (hits > max_hits || (hits == max_hits && page < best_page))
                {
                    max_hits = hits;
                    best_page = page;
                }
            }

            all_pages.erase(std::remove(all_pages.begin(), all_pages.end(), best_page), all_pages.end()); // whats its doing? are we removing all after best page? what  the motto of this line?

            string filename = fs::path(file_path).filename().string();
            cout << "\n"
                 << i + 1 << ". " << filename << " (Score: " << score << ")" << endl;
            string file_pdf = filename;
            file_pdf = file_pdf.substr(0, file_pdf.size() - 4);
            if (best_page != 999999)
            {
                cout << "   ↳ Best Match-> Page: " << best_page << endl;
            }

            if (!all_pages.empty())
            {
                cout << "   ↳ Also mentioned on pages: ";
                int max_pages_to_show = min(5, (int)all_pages.size());
                for (int p = 0; p < max_pages_to_show; p++)
                {
                    cout << all_pages[p] << (p == max_pages_to_show - 1 ? "" : ", ");
                }
                if (all_pages.size() > 5)
                    cout << "...";
                cout << "" << endl;
            }
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
    cout << "------------------------------" << endl;
    cout << "Search latency: " << duration.count() / 1000.0 << " ms\n\n"
         << endl;
}

// stage 6
//  Calculates the total byte size of the corpus folder to detect changes
size_t get_corpus_hash(const string &corpus_path)
{
    try
    {
        // We'll return the latest modification timestamp (seconds since epoch)
        // Consider both the directory itself (for insert/delete) and all files (for edits)
        std::chrono::system_clock::time_point latest_tp = std::chrono::system_clock::from_time_t(0);
        // directory last write time (may change on insert/delete on many filesystems)
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
    catch (...) {
        return 0;
    }
}
bool save_index(const string &filename, size_t corpus_hash)
{
    ofstream out(filename, ios::binary);
    if (!out)
        return false;

    // --- WRITE THE CORPUS HASH FIRST ---
    out.write((char *)&corpus_hash, sizeof(corpus_hash));

    // --- WRITE METRICS ---
    out.write((char *)&total_pages, sizeof(total_pages));
    out.write((char *)&total_indx_words, sizeof(total_indx_words));

    // ---------- ID_allocator ----------
    size_t docs = ID_allocator.size();
    out.write((char *)&docs, sizeof(docs));

    for (const auto &name : ID_allocator)//any way to write it rather than loop
    {
        size_t len = name.size();
        out.write((char *)&len, sizeof(len));
        out.write(name.data(), len);
    }

    // ---------- doc_length ----------
    size_t dl = doc_length.size();
    out.write((char *)&dl, sizeof(dl));
    out.write((char *)doc_length.data(), dl * sizeof(int));

    // ---------- global_index ----------
    size_t words = global_index.size(); //3 loops??? any better way to write it?
    out.write((char *)&words, sizeof(words));

    for (const auto &[word, occurrences] : global_index)
    {
        size_t wordLen = word.size();
        out.write((char *)&wordLen, sizeof(wordLen));
        out.write(word.data(), wordLen);

        size_t occCount = occurrences.size();
        out.write((char *)&occCount, sizeof(occCount));

        for (const auto &occ : occurrences)
        {
            out.write((char *)&occ.docID, sizeof(int));
            out.write((char *)&occ.total_frequency, sizeof(int));

            size_t pageCount = occ.page_positions.size();
            out.write((char *)&pageCount, sizeof(pageCount));

            for (const auto &[page, positions] : occ.page_positions)
            {
                out.write((char *)&page, sizeof(int));

                size_t posCount = positions.size();
                out.write((char *)&posCount, sizeof(posCount));

                out.write((char *)positions.data(),
                          posCount * sizeof(int));
            }
        }
    }

    return true;
}

// stage 6

bool load_index(const string &filename)
{
    ifstream in(filename, ios::binary);

    if (!in)
        return false;
    // --- READ AND SKIP THE CORPUS HASH ---
    size_t dummy_hash;
    in.read((char *)&dummy_hash, sizeof(dummy_hash));

    global_index.clear();
    ID_allocator.clear();
    doc_length.clear();
    // --- READ METRICS ---
    in.read((char *)&total_pages, sizeof(total_pages));
    in.read((char *)&total_indx_words, sizeof(total_indx_words));
    // ---------- ID_allocator ----------
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

    // ---------- doc_length ----------
    size_t dl;
    in.read((char *)&dl, sizeof(dl));

    doc_length.resize(dl);
    in.read((char *)doc_length.data(), dl * sizeof(int));

    // ---------- global_index ----------
    size_t words;
    in.read((char *)&words, sizeof(words));

    for (size_t i = 0; i < words; i++)
    {
        size_t wordLen;
        in.read((char *)&wordLen, sizeof(wordLen));

        string word(wordLen, '\0');
        in.read(&word[0], wordLen);

        size_t occCount;
        in.read((char *)&occCount, sizeof(occCount));

        vector<Occurrence> occs;

        for (size_t j = 0; j < occCount; j++)
        {
            Occurrence occ;

            in.read((char *)&occ.docID, sizeof(int));
            in.read((char *)&occ.total_frequency, sizeof(int));

            size_t pageCount;
            in.read((char *)&pageCount, sizeof(pageCount));

            for (size_t k = 0; k < pageCount; k++)
            {
                int page;
                in.read((char *)&page, sizeof(int));

                size_t posCount;
                in.read((char *)&posCount, sizeof(posCount));

                vector<int> positions(posCount);
                in.read((char *)positions.data(),
                        posCount * sizeof(int));

                occ.page_positions[page] = move(positions);
            }

            occs.push_back(move(occ));
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
        cout << "ERROR: Please enter corpus path." << endl;
        return 1;
    }
    string corpus_path = argv[1];
    string index_file = "core_index.dat";
    auto verify_start = chrono::steady_clock::now();

    // Get the real-time byte size of the corpus folder
    size_t current_hash = get_corpus_hash(corpus_path);
    bool rebuild_needed = true;

    // STAGE 6: Check for a valid, up-to-date binary index
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
            cout << "\n-> Successfully Cache Verified in " << verify_dur.count()/1000.0 << " ms.\n"
                 << endl;
            cout << "\n[System] Cache is up-to-date. Bypassing text parser..." << endl;
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
            cout << "\n\n[System] Corpus changes detected! Invalidating stale cache..." << endl;
            auto verify_end = chrono::steady_clock::now();
            auto verify_dur = chrono::duration_cast<chrono::microseconds>(verify_end - verify_start);
            cout << "\n-> Cache Verification Failed in " << verify_dur.count() / 1000.0 << " ms.\n"
                 << endl;
        }
    }

    if (rebuild_needed)
    {
        cout << "Building Positional Index from scratch: " << corpus_path << "..." << endl;
        auto build_start = chrono::steady_clock::now();
        for (const auto &file : dir_itr(corpus_path))
        {
            int fileID = ID_allocator.size();
            ID_allocator.push_back(file.path().string());

            std::ifstream in_file(file.path().string());

            if (in_file.is_open())
            {
                int total_words = 0;
                unordered_map<string, Occurrence> local_word_tracker; // need more infor about local word tracker

                int current_page_number = 1;
                int absolute_word_position = 0;

                // FIX: Read line-by-line to preserve the '\f' character!
                string line;
                while (getline(in_file, line))
                {
                    // 1. Page Tracking Logic (Catch \f anywhere in the line)
                    if (line.find('\f') != string::npos)
                    {
                        current_page_number++;
                        total_pages++;
                        absolute_word_position = 0; // the plan was to not reset word position after next page// page reset or page break must not become problem for search
                        line.erase(std::remove(line.begin(), line.end(), '\f'), line.end());
                    }

                    // 2. Prepare the line for word extraction
                    std::replace(line.begin(), line.end(), '-', ' ');
                    stringstream line_ss(line);
                    string raw_word;

                    // 3. Extract word by word from the cleaned line
                    while (line_ss >> raw_word)
                    {
                        // FIX 2: Strip ALL non-alphanumeric characters (Destroys Unicode/OCR garbage)
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

                            if (local_word_tracker[raw_word].total_frequency == 0)
                            {
                                local_word_tracker[raw_word].docID = fileID;
                            }

                            local_word_tracker[raw_word].total_frequency++;
                            local_word_tracker[raw_word].page_positions[current_page_number].push_back(absolute_word_position);// doesnt seem i need maps here
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

        // STAGE 6: Save the completed index to disk so we never have to do this again
        auto build_end = chrono::steady_clock::now();
        auto build_dur = chrono::duration_cast<chrono::microseconds>(build_end - build_start);
        cout << "\n-> Successfully build Index of  " << doc_length.size() << " documents in " << build_dur.count()/1000.0 << " ms from scratch.\n"
             << endl;
        cout << "\n[System] Parsing complete. Serializing index to disk..." << endl;
        // cout << "-> Successfully loaded " << doc_length.size() << endl;
        // cout<<"\nTime taken from creaing new Index, and serializing it to disk: "<<boot_dur.count() << " ms.\n"<< endl;
        auto saved_start = chrono::steady_clock::now();
        save_index(index_file, get_corpus_hash(corpus_path));
        auto saved_end = chrono::steady_clock::now();
        auto saved_dur = chrono::duration_cast<chrono::microseconds>(saved_end - saved_start);
        cout << "\n-> Successfully Serialized Index of " << doc_length.size() << " documents in " << saved_dur.count()/1000.0 << " ms from scratch.\n"
             << endl;
    }
    // need to remove all letter that are not simply english or numbers from text, or like  not parse  them
    double mb = fs::file_size(index_file) / (1024.0 * 1024.0); // its in mebibyte

    cout << "Index successfully built! Loaded " << doc_length.size() << " documents.\n"
         << endl;
    cout << fixed << setprecision(2)
         << "Corpus size: " << mb << " MiB\n"
         << endl;
    cout << "Total indexed tokens: " << total_indx_words << "\n"
         << endl;
    cout << "Total pages: " << total_pages << "\n"
         << endl;
    cout << "Total unique tokens: " << global_index.size() << "\n"
         << endl;
    cout << "Welcome User! SearchCore is a file search engine, supporting AND/OR boolean logic.\nBY default all search queries are case insensitive and will be processed in AND logic.\nTo use OR logic, use 'or:' in the beggining of your search query.\nTo use Exact Phrase Search, use 'strict:' in the beggining of your search query.\n"
         << endl;

    string user_query;
    while (true)
    {
        cout << "Enter search query (or type 'exit' to quit): ";
        getline(cin, user_query);

        if (user_query == "!benchmark")
        {
            cout << "\n[System] Initiating 1,000-query benchmark test...\n"
                 << endl;

            vector<string> all_words;
            for (const auto &[word, occ] : global_index)
            {
                all_words.push_back(word);
            }

            if (all_words.empty())
                continue; // means if global index is empty no?

            auto total_start = chrono::steady_clock::now();
            // will run 1000 queries like 200 for one word, 200 for 3 word, and 600 for 2 word

            for (int i = 0; i < 200; i++)
            {
                string random_q = all_words[rand() % all_words.size()];
                search(random_q, true); // are we only gonna test on 100? 2 words?
            }
            for (int i = 0; i < 600; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " +
                                  all_words[rand() % all_words.size()];
                search(random_q, true); // are we only gonna test on 100? 2 words?
            }
            for (int i = 0; i < 200; i++)
            {
                string random_q = all_words[rand() % all_words.size()] + " " +
                                  all_words[rand() % all_words.size()] + " " +
                                  all_words[rand() % all_words.size()];
                search(random_q, true); // are we only gonna test on 100? 2 words?
            }

            auto total_end = chrono::steady_clock::now();
            auto total_duration = chrono::duration_cast<chrono::microseconds>(total_end - total_start);

            double avg_latency_ms = (total_duration.count() / 1000.0) / 1000;

            cout << "[System] Benchmark Complete!" << endl;
            cout << "-> Total Time for " << 1000 << " queries: " << total_duration.count() / 1000.0 << " ms" << endl;
            cout << "-> Average Latency: " << avg_latency_ms << " ms per query\n"
                 << endl;
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
