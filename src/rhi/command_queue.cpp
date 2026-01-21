//
// Created by Zero on 10/07/2022.
//

#include "command_queue.h"
#include "command.h"

namespace ocarina {

CommandList &CommandList::operator<<(ocarina::Command *command) noexcept {
    push_back(command);
    return *this;
}

CommandList &CommandList::operator<<(const vector<Command *> &commands) noexcept {
    append(*this, commands);
    return *this;
}

CommandList &CommandList::operator<<(std::function<void()> func) noexcept {
    return (*this) << HostFunctionCommand::create(ocarina::move(func), true);
}

void CommandList::accept(CommandVisitor &visitor) const noexcept {
    for (const Command *command : (*this)) {
        command->accept(visitor);
    }
}

void CommandList::recycle() noexcept {
    for (Command *command :  (*this)) {
        command->recycle();
    }
}

void CommandQueue::recycle() noexcept {
    for (Command *command : commands_) {
        command->recycle();
    }
}

void CommandQueue::pop_back() {
    commands_.pop_back();
}

void CommandQueue::clear() noexcept{
    recycle();
    commands_.clear();
}

}// namespace ocarina