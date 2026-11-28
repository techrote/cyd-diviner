#include "../src/dsp.h"
#include "../src/vox.h"
#include "../src/dsp_score.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

int main() {
    dsp::prepare();
    double maxSineError = 0, energy = 0;
    dsp::Osc osc; osc.hz(440.f);
    for (int i = 0; i < dsp::RATE; ++i) {
        const double angle = double(osc.phase) * (6.283185307179586 / 4294967296.0);
        const float x = osc.sine();
        maxSineError = std::max(maxSineError, std::abs(x - std::sin(angle)));
        energy += x * x;
    }
    assert(maxSineError < 6e-6);
    assert(std::abs(std::sqrt(energy / dsp::RATE) - std::sqrt(0.5)) < 1e-4);
    const double actualHz = double(dsp::hzToInc(440.f)) * dsp::RATE / 4294967296.0;
    assert(std::abs(actualHz - 440) < double(dsp::RATE) / 4294967296.0);
    assert(dsp::hzToInc(-1) == 0 && dsp::hzToInc(NAN) == 0 && dsp::hzToInc(INFINITY) == 0);
    for (float phase : {-100000.f, -1.5f, 0.f, .5f, 100000.f, NAN, INFINITY}) {
        const float x = osc.pmSine(phase); assert(std::isfinite(x) && std::abs(x) <= 1.00001f);
    }
    dsp::Osc slow, equivalent; slow.hz(5.2f); equivalent.hz(5.2f);
    for (int t = 0; t < 1500; ++t) {
        const float a = slow.sineStrided(16);
        const float b = equivalent.sine();
        assert(a == b);
        for (int i = 1; i < 16; ++i) (void)equivalent.sine();
        assert(slow.phase == equivalent.phase);
    }

    dsp::Decay envelope; envelope.time(1.f); envelope.trigger();
    for (int i = 0; i < dsp::RATE; ++i) envelope.tick();
    const float envelopeError = std::abs(envelope.v - std::exp(-1.f));
    assert(envelopeError < 0.001f); // float coefficient rounding at 24/48 kHz
    envelope.time(0); envelope.trigger(); assert(envelope.tick() == 0);

    double filterPeak = 0;
    for (float hz : {0.f, 50.f, 330.f, 850.f, 1900.f, 3050.f, 20000.f, -1.f, NAN}) {
        for (float resonance : {0.f, .5f, .9f, 1.f, 2.f, -1.f, NAN}) {
            dsp::SVF f; f.set(hz, resonance);
            for (int i = 0; i < dsp::RATE / 2; ++i) {
                const float x = f.bp(i == 0 ? 1.f : 0.f);
                assert(std::isfinite(x));
                filterPeak = std::max(filterPeak, double(std::abs(x)));
                assert(std::abs(x) < 8);
            }
        }
    }
    // Measure resonant band-pass peak from impulse response, not just stored
    // coefficients. The scan resolves 10 Hz; formant width is much larger.
    for (int target : {330, 560, 850, 1300, 1900, 2300, 2700, 3050}) {
        dsp::SVF f; f.set(float(target), .9f);
        std::vector<float> impulse(2048);
        for (int i = 0; i < int(impulse.size()); ++i) impulse[i] = f.bp(i == 0 ? 1.f : 0.f);
        // For this damped Chamberlin recurrence the BP resonance is at
        // cos(w)=1-f^2/(2*sqrt(1-f*q)), not exactly the nominal f mapping.
        const double predicted = std::acos(1 - f.f*f.f/(2*std::sqrt(1-f.f*f.q))) * dsp::RATE / 6.283185307179586;
        double peak = -1; int best = 0;
        for (int hz = std::max(10,target - 400); hz <= target + 400; hz += 10) {
            double real = 0, imag = 0;
            for (int i = 0; i < int(impulse.size()); ++i) {
                const double phase = 6.283185307179586 * hz * i / dsp::RATE;
                real += impulse[i] * std::cos(phase); imag += impulse[i] * std::sin(phase);
            }
            const double power = real*real + imag*imag;
            if (power > peak) { peak = power; best = hz; }
        }
        assert(std::abs(best - predicted) <= 11); // 10 Hz DFT search resolution
        std::printf("formant_filter nominal_hz=%d predicted_hz=%.2f measured_peak_hz=%d\n", target,predicted,best);
    }

    dsp::Delay<8> fd; dsp::Delay<7> odd; dsp::Delay16<8> qd;
    double quantError = 0;
    for (int i = 0; i < 10000; ++i) {
        const float x = std::sin(float(i) * .03f) * 1.8f;
        fd.put(x); qd.put(x); odd.put(x);
        assert(fd.tap(1) == x && odd.tap(1) == x);
        assert(fd.tap(9) == x && fd.tap(-7) == x && odd.tap(8) == x && odd.tap(-6) == x);
        quantError = std::max(quantError,double(std::abs(qd.tap(1)-x)));
    }
    assert(quantError <= 1.0/16384 + 1e-7);
    qd.put(NAN); assert(qd.tap(1) == 0);
    qd.put(INFINITY); assert(qd.tap(1) <= 1.99f);
    assert(dsp::toPcm(NAN) == 0 && dsp::toPcm(INFINITY) == 30000 && dsp::toPcm(-INFINITY) == -30000);

    double maxVoice = 0;
    for (char vowel : {'a','e','i','o','u'}) {
        vox::Voice a, b; a.legato = b.legato = 1;
        a.say(vowel,220,dsp::RATE/4); b.say(vowel,220,dsp::RATE/4);
        double sum = 0;
        for (int i = 0; i < dsp::RATE/2; ++i) {
            const float x = a.tick(), y = b.tick();
            assert(x == y && std::isfinite(x));
            maxVoice = std::max(maxVoice,double(std::abs(x))); sum += x*x;
        }
        assert(sum > .01 && a.amp < 1e-4);
    }
    vox::Voice invalid; invalid.sing(15,-99,999,15,220,2400);
    for (int i = 0; i < dsp::RATE; ++i) assert(std::isfinite(invalid.tick()));

    using namespace glyphsong_score;
    SNote score[MAXN]{}, repeat[MAXN]{};
    int n = compose("present day3_. present time?",score);
    assert(n > 0 && n <= MAXN);
    assert(n == compose("present day3_. present time?",repeat));
    assert(std::memcmp(score,repeat,sizeof score) == 0);
    bool held = false, fixed = false, rest = false, glide = false;
    for (int i = 0; i < n; ++i) {
        assert(score[i].beats > 0 && score[i].beats <= 48);
        assert(score[i].at + score[i].len <= 40);
        held |= score[i].beats > 1; fixed |= score[i].fixed; rest |= score[i].v1 == REST;
        glide |= score[i].v1 != REST && score[i].v1 != score[i].v2;
    }
    assert(held && fixed && rest && glide);
    char fuzz[41]; dsp::Noise rng;
    for (int trial = 0; trial < 10000; ++trial) {
        for (int i = 0; i < 40; ++i) fuzz[i] = char(32 + rng.next()%95);
        fuzz[40] = 0;
        const int count = compose(fuzz,score);
        assert(count >= 0 && count <= MAXN);
        for (int i = 0; i < count; ++i) {
            assert(score[i].beats > 0 && score[i].at + score[i].len <= 40);
            assert(score[i].v1 == REST || (score[i].v1 < 6 && score[i].v2 < 6));
        }
    }
    std::printf("PASS rate=%d sine_error=%.9g pitch_hz=%.9f envelope_error=%.9g svf_peak=%.6f delay16_error=%.9g vox_peak=%.6f score_fuzz=10000\n", dsp::RATE,maxSineError,actualHz,envelopeError,filterPeak,quantError,maxVoice);
}
