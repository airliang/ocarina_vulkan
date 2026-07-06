
#include "entity_component_system.h"

namespace ocarina {

EntityComponentSystem& EntityComponentSystem::instance() noexcept {
    static EntityComponentSystem ecs;
    return ecs;
}

}// namespace ocarina
