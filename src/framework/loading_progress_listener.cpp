#include "loading_progress_listener.h"

#include <algorithm>

namespace ocarina {

void LoadingProgressListener::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = {};
    state_.store(State::Idle, std::memory_order_release);
    progress_.store(-1.f, std::memory_order_release);
}

void LoadingProgressListener::begin(const string& title, uint32_t total_steps) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = {};
    snapshot_.state = State::Running;
    snapshot_.title = title;
    snapshot_.total_steps = total_steps;
    snapshot_.current_step = 0;
    update_progress_locked();
    state_.store(State::Running, std::memory_order_release);
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::set_total_steps(uint32_t total_steps) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.total_steps = total_steps;
    update_progress_locked();
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::set_phase(const string& phase) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.phase = phase;
}

void LoadingProgressListener::set_message(const string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.message = message;
}

void LoadingProgressListener::set_step(uint32_t current_step, uint32_t total_steps) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.total_steps = total_steps;
    snapshot_.current_step = total_steps > 0 ? std::min(current_step, total_steps) : current_step;
    update_progress_locked();
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::advance(uint32_t delta) {
    if (delta == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != State::Running) {
        return;
    }

    if (snapshot_.total_steps > 0) {
        const uint32_t next_step = snapshot_.current_step + delta;
        snapshot_.current_step = std::min(next_step, snapshot_.total_steps);
    } else {
        snapshot_.current_step += delta;
    }
    update_progress_locked();
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::set_progress(float normalized_progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != State::Running) {
        return;
    }

    snapshot_.progress = std::clamp(normalized_progress, 0.f, 1.f);
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::complete() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = State::Completed;
    if (snapshot_.total_steps > 0) {
        snapshot_.current_step = snapshot_.total_steps;
        snapshot_.progress = 1.f;
    } else {
        snapshot_.progress = 1.f;
    }
    state_.store(State::Completed, std::memory_order_release);
    progress_.store(snapshot_.progress, std::memory_order_release);
}

void LoadingProgressListener::fail(const string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = State::Failed;
    snapshot_.message = message;
    state_.store(State::Failed, std::memory_order_release);
}

LoadingProgressListener::Snapshot LoadingProgressListener::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

float LoadingProgressListener::progress() const noexcept {
    return progress_.load(std::memory_order_acquire);
}

LoadingProgressListener::State LoadingProgressListener::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

bool LoadingProgressListener::is_indeterminate() const noexcept {
    return progress() < 0.f;
}

void LoadingProgressListener::update_progress_locked() noexcept {
    if (snapshot_.total_steps == 0) {
        snapshot_.progress = -1.f;
        return;
    }

    snapshot_.progress = static_cast<float>(snapshot_.current_step) / static_cast<float>(snapshot_.total_steps);
}

}// namespace ocarina
