#include "Camera.h"
#include "IO.h"
#include "Logger.h"
#include "ProgramOptions.h"
#include "Runtime.h"
#include "Timer.h"
#include "UI.h"
#include "config/Build.h"

using namespace IG;

struct SectionTimer {
    Timer timer;
    size_t duration_ms = 0;

    inline void start() { timer.start(); }
    inline void stop() { duration_ms += timer.stopMS(); }
};

int main(int argc, char** argv)
{
    ProgramOptions cmd(argc, argv, ApplicationType::View, "Interactive viewer");
    if (cmd.ShouldExit)
        return EXIT_SUCCESS;

    RuntimeOptions opts;
    cmd.populate(opts);

    if (!cmd.Quiet)
        std::cout << Build::getCopyrightString() << std::endl;

    if (cmd.InputScene.empty()) {
        IG_LOG(L_ERROR) << "No input file given" << std::endl;
        return EXIT_FAILURE;
    }

    if (cmd.SPPMode != SPPMode::Fixed && !cmd.SPP.has_value()) {
        IG_LOG(L_ERROR) << "No valid spp count given. Required by the capped or continous spp mode" << std::endl;
        return EXIT_FAILURE;
    }

    SectionTimer timer_all;
    SectionTimer timer_loading;
    timer_all.start();
    timer_loading.start();

    std::unique_ptr<Runtime> runtime;
    try {
        runtime = std::make_unique<Runtime>(cmd.InputScene, opts);
    } catch (const std::exception& e) {
        IG_LOG(L_ERROR) << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    timer_loading.stop();

    const auto def = runtime->initialCameraOrientation();
    Camera camera(cmd.EyeVector().value_or(def.Eye), cmd.DirVector().value_or(def.Dir), cmd.UpVector().value_or(def.Up));
    runtime->setup();
    runtime->setParameter("__camera_eye", camera.Eye);
    runtime->setParameter("__camera_dir", camera.Direction);
    runtime->setParameter("__camera_up", camera.Up);

    const size_t SPI          = runtime->samplesPerIteration();
    const size_t desired_iter = static_cast<size_t>(std::ceil(cmd.SPP.value_or(0) / (float)SPI));

    if (cmd.SPP.has_value() && (cmd.SPP.value() % SPI) != 0)
        IG_LOG(L_WARNING) << "Given spp " << cmd.SPP.value() << " is not a multiple of the spi " << SPI << ". Using spp " << desired_iter * SPI << " instead" << std::endl;

    std::unique_ptr<UI> ui;
    try {
        ui = std::make_unique<UI>(runtime.get(), runtime->framebufferWidth(), runtime->framebufferHeight(), runtime->isDebug());

        // Setup initial travelspeed
        BoundingBox bbox = runtime->sceneBoundingBox();
        bbox.extend(camera.Eye);
        ui->setTravelSpeed(bbox.diameter().maxCoeff() / 50);
    } catch (...) {
        return EXIT_FAILURE;
    }

    auto lastDebugMode = ui->currentDebugMode();

    IG_LOG(L_INFO) << "Started rendering..." << std::endl;

    bool running    = true;
    bool done       = false;
    uint64_t timing = 0;
    uint32_t frames = 0;
    std::vector<double> samples_sec;

    SectionTimer timer_input;
    SectionTimer timer_ui;
    SectionTimer timer_render;
    while (!done) {
        bool prevRun = running;

        timer_input.start();
        uint32 iter = runtime->currentIterationCount();
        done        = ui->handleInput(iter, running, camera);
        timer_input.stop();

        if (lastDebugMode != ui->currentDebugMode()) {
            runtime->setParameter("__debug_mode", (int)ui->currentDebugMode());
            runtime->reset();
            lastDebugMode = ui->currentDebugMode();
        } else if (iter != runtime->currentIterationCount()) {
            runtime->setParameter("__camera_eye", camera.Eye);
            runtime->setParameter("__camera_dir", camera.Direction);
            runtime->setParameter("__camera_up", camera.Up);
            runtime->reset();
        }

        if (running) {
            if (cmd.SPPMode != SPPMode::Capped || runtime->currentIterationCount() < desired_iter) {
                if (cmd.SPPMode == SPPMode::Continous && runtime->currentIterationCount() >= desired_iter) {
                    runtime->reset();
                }

                auto ticks = std::chrono::high_resolution_clock::now();

                timer_render.start();
                runtime->step();
                timer_render.stop();

                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - ticks).count();

                if (cmd.SPPMode == SPPMode::Fixed && desired_iter != 0) {
                    samples_sec.emplace_back(1000.0 * double(SPI * runtime->framebufferWidth() * runtime->framebufferHeight()) / double(elapsed_ms));
                    if (samples_sec.size() == desired_iter)
                        break;
                }

                frames++;
                timing += elapsed_ms;
                if (frames > 10 || timing >= 2000) {
                    const double frames_sec  = double(frames) * 1000.0 / double(timing);
                    const double samples_sec = (runtime->currentIterationCount() == 0) ? 0.0 : frames_sec * runtime->currentSampleCount() / (float)runtime->currentIterationCount();

                    std::ostringstream os;
                    os << "Ignis [" << frames_sec << " FPS, "
                       << samples_sec << " SPS, "
                       << runtime->currentSampleCount() << " "
                       << "sample" << (runtime->currentSampleCount() > 1 ? "s" : "") << "]";
                    ui->setTitle(os.str().c_str());

                    frames = 0;
                    timing = 0;
                }
            } else {
                std::ostringstream os;
                os << "Ignis [Capped, "
                   << runtime->currentSampleCount() << " "
                   << "sample" << (runtime->currentSampleCount() > 1 ? "s" : "") << "]";
                ui->setTitle(os.str().c_str());
            }
        } else {
            frames++;

            if (prevRun != running || frames > 100) {
                std::ostringstream os;
                os << "Ignis [Paused, "
                   << runtime->currentSampleCount() << " "
                   << "sample" << (runtime->currentSampleCount() > 1 ? "s" : "") << "]";
                ui->setTitle(os.str().c_str());
                frames = 0;
                timing = 0;
            }
        }

        timer_ui.start();
        ui->update(runtime->currentIterationCount(), runtime->currentSampleCount());
        timer_ui.stop();
    }

    ui.reset();

    SectionTimer timer_saving;
    timer_saving.start();
    if (!cmd.Output.empty()) {
        if (!saveImageOutput(cmd.Output, *runtime))
            IG_LOG(L_ERROR) << "Failed to save EXR file '" << cmd.Output << "'" << std::endl;
        else
            IG_LOG(L_INFO) << "Result saved to '" << cmd.Output << "'" << std::endl;
    }
    timer_saving.stop();

    timer_all.stop();

    auto stats = runtime->getStatistics();
    if (stats) {
        IG_LOG(L_INFO)
            << stats->dump(runtime->currentIterationCount(), cmd.AcquireFullStats)
            << "  Iterations: " << runtime->currentIterationCount() << std::endl
            << "  SPP: " << runtime->currentSampleCount() << std::endl
            << "  SPI: " << SPI << std::endl
            << "  Time: " << timer_all.duration_ms << "ms" << std::endl
            << "    Loading> " << timer_loading.duration_ms << "ms" << std::endl
            << "    Input>   " << timer_input.duration_ms << "ms" << std::endl
            << "    UI>      " << timer_ui.duration_ms << "ms" << std::endl
            << "    Render>  " << timer_render.duration_ms << "ms" << std::endl
            << "    Saving>  " << timer_saving.duration_ms << "ms" << std::endl;
    }

    runtime.reset();

    if (!samples_sec.empty()) {
        auto inv = 1.0e-6;
        std::sort(samples_sec.begin(), samples_sec.end());
        IG_LOG(L_INFO) << "# " << samples_sec.front() * inv
                       << "/" << samples_sec[samples_sec.size() / 2] * inv
                       << "/" << samples_sec.back() * inv
                       << " (min/med/max Msamples/s)" << std::endl;
    }

    return EXIT_SUCCESS;
}