#include <cstddef>
#include <cstdint>

namespace Vocabulary {

typedef struct word {
  uint32_t id;
  uint8_t data[33];
  double weight;
} Word;

extern Word default_dictionary[];
extern std::size_t default_dictionary_size;

} // namespace Vocabulary