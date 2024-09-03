#include <cstdlib>
/**
 * Returns a random number in the range [min..max]
 * @param min
 * @param max
 * @return random T number in [min..max]
 */
template <class T> T random(T min, T max) {
  return ((T)rand() / (T)RAND_MAX) * (max - min) + min;
}
