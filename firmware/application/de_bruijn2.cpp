#include "de_bruijn2.hpp"


/*
t - init at 1
p - init at 1
n - number of charset permutations of charset elements(could also be bits if 01 is charset)
maxlen - buffer size for output (size of sequence) 
k - number of charset elements (length of charset string)
a - temporary array for calculations
sequence - output buffer - should be allocated
charset - elements for permutations
*/
void de_bruijn2::init() {
  abrt = false;
}

void de_bruijn2::abort() {
  abrt = true;
}


void de_bruijn2::db(uint32_t t, 
                    uint32_t p, 
                    uint32_t n, 
                    uint32_t maxlen, 
                    uint16_t k, 
                    uint8_t* a, 
                    char* sequence, 
                    char* charset) {
  uint16_t seq_len = strlen(sequence);
  if (seq_len == maxlen || abrt) {
    return;
  }
  if (t > n) {
    //everytime a full value is calculated
    if (n % p == 0) {
      //compact full value
      for (uint16_t j = 1; j <= p; ++j) {
        sequence[seq_len] = charset[a[j]];
        seq_len++;
        if (seq_len == maxlen) {
          return;
        }
      }
    }
  } else {
    a[t] = a[t - p];
    de_bruijn2::db(t + 1, p, n, maxlen, k, a, sequence, charset);
    for (uint16_t j = a[t - p] + 1; j < k; ++j) {
      a[t] = j;
      de_bruijn2::db(t + 1, t, n, maxlen, k, a, sequence, charset);
    }
  }
}

void de_bruijn2::db_callback(uint32_t t, 
                    uint32_t p, 
                    uint32_t n, 
                    uint32_t maxlen, 
                    int32_t seq_len, 
                    uint16_t k, 
                    uint8_t* a, 
                    char* charset, 
                    std::function<void(char* seq_part, uint8_t seq_part_len)> callback) {
  
  if (abrt) {
    return;
  }

  //Avoid cyclical memorization requirement, output codes with charsets from 1 on.
  //This will increase the number of codes by k-1                     
  if (seq_len == -1) {    
    for (uint8_t j = 1; j < k; j++) {
        char init_seq[n];
        for (uint8_t i = 0; i <= n; i++) {
            init_seq[i] = charset[j];
        }
        callback(init_seq, n);
    }
    seq_len++;
  }

  if (t > n) {
    //everytime a full value is calculated
    if (n % p == 0) {
      //compact full value
      char sequence[p];
      uint8_t seq_part_len = 0;
      for (uint16_t j = 1; j <= p; ++j) {
        sequence[j-1] = charset[a[j]];
        seq_len++;
        seq_part_len++;
      }
      if (seq_part_len > 0)
        callback(sequence, seq_part_len);
    }
  } else {
    a[t] = a[t - p];
    de_bruijn2::db_callback(t + 1, p, n, maxlen, seq_len, k, a, charset, callback);
    for (uint16_t j = a[t - p] + 1; j < k; ++j) {
      a[t] = j;
      de_bruijn2::db_callback(t + 1, t, n, maxlen, seq_len, k, a, charset, callback);
    }

    for (uint16_t j = 0; j < n; ++j) {

    }
  }
}