#include <vector>
#include <cmath>
#include <cstdlib>
#define maxPrimeIndex 10

class BrownNoiseGenerator
{
public:
  BrownNoiseGenerator(int oct, double pers, int primeInd);

  void GenerateBrownNoiseMap(std::vector<uint32_t>& map, int width, int height);

private:
  ~BrownNoiseGenerator();

  double Noise(int i, int x, int y);

  double SmoothedNoise(int i, int x, int y);

  double Interpolate(double a, double b, double x);

  double InterpolatedNoise(int i, double x, double y);

  double ValueNoise_2D(double x, double y);

  int numOctaves;
  double persistence;
  int primeIndex;

  int primes[maxPrimeIndex][3] = {
    { 995615039, 600173719, 701464987 },
    { 831731269, 162318869, 136250887 },
    { 174329291, 946737083, 245679977 },
    { 362489573, 795918041, 350777237 },
    { 457025711, 880830799, 909678923 },
    { 787070341, 177340217, 593320781 },
    { 405493717, 291031019, 391950901 },
    { 458904767, 676625681, 424452397 },
    { 531736441, 939683957, 810651871 },
    { 997169939, 842027887, 423882827 }
  };
}; 