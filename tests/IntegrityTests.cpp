#include "Integrity.hpp"
#include <cstdlib>
#include <iostream>

using namespace context;
namespace {
int checks = 0;
void check(bool value, char const* description) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
}

int main() {
    AttemptIntegrity guard;
    guard.collision(true, false, false);
    check(!guard.blocked(), "anticheat spike is not a noclip collision");
    guard.collision(false, true, false);
    check(!guard.blocked(), "normal death remains valid");
    guard.collision(false, false, true);
    check(!guard.blocked(), "completion animation does not look like noclip");
    guard.collision(false, false, false);
    check(guard.blocked(), "suppressed death latches noclip");
    guard.damageFlags(false, false);
    check(guard.blocked(), "turning noclip off cannot clean the current attempt");
    guard.reset();
    guard.damageFlags(false, true);
    check(guard.blocked(), "second built-in ignore-damage flag is checked");
    guard.reset();
    guard.damageFlags(true, false);
    check(guard.blocked(), "first built-in ignore-damage flag is checked");

    for (double dt : {1./30, 1./60, 1./144, 1./240, 1./1000, .25, 1.5}) {
        guard.reset();
        guard.clock(dt, dt, 1.);
        check(!guard.blocked(), "equal frame deltas stay clean at any FPS or lag spike");
        guard.clock(dt, dt * .5, 1.);
        check(guard.blocked(), "scheduler slowdown is detected");
        guard.reset();
        guard.clock(dt, dt * 2., 1.);
        check(guard.blocked(), "scheduler speedup is detected");
    }
    guard.reset();
    guard.clock(0, 0, 1.);
    guard.clock(1./240, 1./240 + .0000001, 1.);
    check(!guard.blocked(), "zero tick and float rounding are harmless");
    guard.clock(1./60, 1./60, .5);
    check(guard.blocked(), "scheduler time scale is detected even without a delta hook");
    guard.clock(1./60, 1./60, 1.);
    check(guard.blocked(), "restoring speed mid-attempt cannot restore validity");
    guard.reset();
    guard.clock(1./60, 1./60, 2.);
    check(guard.blocked(), "fast scheduler time scale is detected");

    Window shortRun; shortRun.start = 10; shortRun.end = 20;
    Window laterRun; laterRun.start = 20; laterRun.end = 30;
    RunTracker first, later;
    auto begin = [&] {
        guard.reset();
        first.begin(shortRun, 10, Mode::Verify, true, false, 1);
        later.begin(laterRun, 10, Mode::Verify, true, false, 1);
    };
    begin();
    auto pass = first.sample(21, 1.5, 2, 2);
    check(pass.has_value(), "short target is reached without ending gameplay");
    guard.queue(1, *pass);
    auto failure = later.finish(Outcome::Failure, 25, 3, 4, 2);
    guard.queue(2, *failure);
    auto clean = guard.takeResults();
    check(clean.size() == 2 && clean[0].attempt.outcome == Outcome::Success &&
          clean[1].attempt.outcome == Outcome::Failure, "clean continuation keeps pass and later death");
    check(guard.takeResults().empty(), "results can only be committed once");

    begin();
    guard.queue(1, *first.sample(21, 1.5, 2, 2));
    guard.collision(false, false, false);
    guard.queue(2, *later.sample(31, 3, 4, 2));
    check(guard.takeResults().empty(), "late noclip discards earlier queued pass and later results");
    begin();
    guard.queue(1, *first.sample(21, 1.5, 2, 2));
    guard.clock(1./60, 1./120, 1.);
    check(guard.takeResults().empty(), "late speedhack discards earlier target pass");
    guard.reset();
    guard.queue(1, *pass);
    check(!guard.blocked() && guard.takeResults().size() == 1, "clean restart restores recording");
    guard.queue(1, *pass);
    guard.reset();
    check(guard.takeResults().empty(), "reset cannot leak pending results into another attempt");
    std::cout << checks << " integrity checks passed\n";
}
