#include <mlt++/MltConsumer.h>
#include <mlt++/MltFactory.h>
#include <mlt++/MltProducer.h>
#include <mlt++/MltProfile.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

class FactoryLifetime final {
public:
    FactoryLifetime()
        : repository_(Mlt::Factory::init(std::getenv("MLT_REPOSITORY"))) {}
    ~FactoryLifetime() { Mlt::Factory::close(); }
    [[nodiscard]] bool valid() const noexcept { return repository_ != nullptr; }

private:
    Mlt::Repository* repository_;
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: ytp_media_spike <input-media> <output-media> [--full]\n";
        return 2;
    }
    const bool renderFullInput = argc == 4 && std::string_view{argv[3]} == "--full";
    if (argc == 4 && !renderFullInput) {
        std::cerr << "Unknown option: " << argv[3] << '\n';
        return 2;
    }

    std::cerr << "[spike] initializing MLT repository\n";
    FactoryLifetime factory;
    if (!factory.valid()) {
        std::cerr << "MLT repository initialization failed.\n";
        return 3;
    }

    std::cerr << "[spike] creating profile\n";
    Mlt::Profile profile;
    profile.set_explicit(false);
    std::cerr << "[spike] probing source profile\n";
    Mlt::Producer probe(profile, "avformat", argv[1]);
    if (!probe.is_valid()) {
        std::cerr << "MLT could not open input: " << argv[1] << '\n';
        return 4;
    }
    profile.from_producer(probe);

    // Reopen against the detected source profile. Otherwise MLT can report a
    // frame length quantized to its default profile rather than the media FPS.
    std::cerr << "[spike] opening producer with detected profile\n";
    Mlt::Producer source(profile, "avformat", argv[1]);
    if (!source.is_valid()) {
        std::cerr << "MLT could not reopen input with its detected profile.\n";
        return 4;
    }

    const int totalFrames = source.get_length();
    const double framesPerSecond = source.get_fps();
    if (totalFrames <= 0 || framesPerSecond <= 0.0) {
        std::cerr << "Input has invalid duration or frame rate.\n";
        return 5;
    }

    // Repeated random-access seeks are a foundation acceptance check: timeline
    // scrubbing must not accumulate positional error or deadlock.
    for (int iteration = 0; iteration < 512; ++iteration) {
        const int target = static_cast<int>((static_cast<long long>(iteration) * 7'919) % totalFrames);
        source.seek(target);
        if (source.position() != target) {
            std::cerr << "Seek mismatch at iteration " << iteration << ": expected "
                      << target << ", got " << source.position() << '\n';
            return 6;
        }
    }

    const int inFrame = renderFullInput
        ? 0
        : std::min(totalFrames - 1, std::max(0, static_cast<int>(std::lround(framesPerSecond))));
    const int desiredLength = renderFullInput
        ? totalFrames
        : std::max(1, static_cast<int>(std::lround(framesPerSecond * 2.0)));
    const int outFrame = renderFullInput
        ? totalFrames - 1
        : std::min(totalFrames - 1, inFrame + desiredLength - 1);
    source.set_in_and_out(inFrame, outFrame);

    // Exercise random-access seeking before rendering the selected range.
    source.seek(0);
    if (source.position() != 0) {
        std::cerr << "Relative seek to selected In point failed.\n";
        return 6;
    }
    source.seek(source.get_playtime() / 2);
    const int midpoint = source.position();
    source.seek(0);

    std::cerr << "[spike] creating render consumer\n";
    Mlt::Consumer render(profile, "avformat", argv[2]);
    if (!render.is_valid()) {
        std::cerr << "MLT avformat consumer is unavailable.\n";
        return 7;
    }
    render.set("f", "mp4");
    render.set("vcodec", "libx264");
    render.set("acodec", "aac");
    render.set("movflags", "+faststart");
    render.set("preset", "ultrafast");
    render.set("crf", 32);
    render.set("real_time", -1);
    render.set("terminate_on_pause", 1);
    std::cerr << "[spike] rendering selected range\n";
    if (render.connect(source) != 0 || render.run() != 0) {
        std::cerr << "MLT render failed.\n";
        return 8;
    }

    std::cout << "MLT media spike passed\n"
              << "  input frames: " << totalFrames << '\n'
              << "  fps: " << framesPerSecond << '\n'
              << "  profile: " << profile.width() << 'x' << profile.height() << '\n'
              << "  selected frames: " << inFrame << ".." << outFrame << '\n'
              << "  midpoint seek: " << midpoint << '\n'
              << "  output: " << argv[2] << '\n';
    return 0;
}
