#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

#ifndef __DE_BRUIJN_H2__
#define __DE_BRUIJN_H2__

struct de_bruijn2 {
  public:
    void init();
    void abort();

    bool abrt { false };
    void db(uint32_t t, uint32_t p, uint32_t n, uint32_t maxlen, uint16_t k, uint8_t* a, char* sequence, char* charset);
    void db_callback(uint32_t t, uint32_t p, uint32_t n, uint32_t maxlen, uint16_t seqlen, uint16_t k, uint8_t* a, char* charset, std::function<void(char* seq_part, uint8_t seq_part_len)> callback);
};

#endif/*__DE_BRUIJN_H2__*/


