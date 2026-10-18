#include "Spectrum.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

// Failure cases: silence/DC, frequency placement, amplitude response, stereo phase,
// right-only input, split frames, non-finite samples, reset, and invalid formats.
namespace
{
void Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::vector<float> Tone(float frequency, float amplitude, int mode = 0, int rate = 48000)
{
  std::vector<float> pcm(starlight::Spectrum::Window * 2);
  for (size_t frame = 0; frame < pcm.size() / 2; ++frame)
  {
    const float sample = amplitude * std::sin(6.28318530718f * frequency * frame / rate);
    pcm[frame * 2] = mode == 1 ? 0 : sample;
    pcm[frame * 2 + 1] = mode == 2 ? -sample : sample;
  }
  return pcm;
}

float Peak(const starlight::Spectrum& spectrum)
{
  return *std::max_element(spectrum.Levels().begin(), spectrum.Levels().end());
}
}

int main()
{
  starlight::Spectrum spectrum;
  Check(spectrum.Reset(2, 48000), "accept stereo PCM");
  std::vector<float> silence(starlight::Spectrum::Window * 2, 0);
  spectrum.Push(silence.data(), silence.size());
  Check(Peak(spectrum) == 0, "silence produces no energy");
  auto low = Tone(375, .5f);
  spectrum.Push(low.data(), low.size());
  Check(Peak(spectrum) > .5f, "audible tone produces energy");
  const auto lowBand = std::max_element(spectrum.Levels().begin(), spectrum.Levels().end()) - spectrum.Levels().begin();
  auto high = Tone(6000, .5f);
  spectrum.Push(high.data(), high.size());
  const auto highBand = std::max_element(spectrum.Levels().begin(), spectrum.Levels().end()) - spectrum.Levels().begin();
  Check(highBand > lowBand + 40, "higher tone moves clockwise to a higher band");
  const float loud = Peak(spectrum);
  auto quiet = Tone(6000, .005f);
  spectrum.Push(quiet.data(), quiet.size());
  Check(Peak(spectrum) < loud - .3f, "quiet audio produces shorter bars");
  for (int mode : {1, 2})
  {
    spectrum.Reset(2, 48000);
    auto stereo = Tone(375, .5f, mode);
    // Deliberately split inside an interleaved frame.
    spectrum.Push(stereo.data(), 113);
    spectrum.Push(stereo.data() + 113, stereo.size() - 113);
    Check(Peak(spectrum) > .5f, "right-only and opposite-phase audio remain visible");
  }
  spectrum.Reset(2, 48000);
  std::vector<float> dc(silence.size(), .75f);
  spectrum.Push(dc.data(), dc.size());
  Check(Peak(spectrum) < .01f, "DC offset does not create a spectrum");
  dc[0] = std::numeric_limits<float>::quiet_NaN();
  dc[1] = std::numeric_limits<float>::infinity();
  spectrum.Push(dc.data(), dc.size());
  for (float level : spectrum.Levels())
    Check(std::isfinite(level) && level >= 0 && level <= 1, "invalid samples cannot escape level bounds");
  spectrum.Push(nullptr, 100);
  Check(!spectrum.Reset(0, 48000), "reject zero channels");
  Check(!spectrum.Reset(9, 48000), "reject excess channels");
  Check(!spectrum.Reset(2, 0), "reject zero sample rate");
  for (int rate : {8000, 44100, 48000, 192000})
  {
    Check(spectrum.Reset(2, rate), "accept supported sample rates");
    Check(Peak(spectrum) == 0, "reset removes previous track energy");
    auto tone = Tone(750, .5f, 0, rate);
    spectrum.Push(tone.data(), tone.size());
    Check(Peak(spectrum) > .4f, "sample rate determines frequency bins");
  }
  Check(spectrum.Reset(1, 48000), "accept mono PCM");
  spectrum.Push(low.data(), 1);
  Check(Peak(spectrum) == 0, "partial windows never read beyond available samples");
  std::cout << "PASS: spectrum failure cases\n";
}
