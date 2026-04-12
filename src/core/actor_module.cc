#include "actor_module.h"

namespace ac {

ActorModule::ActorModule(std::size_t id, MailBoxPtr mailbox)
    : actor_id_(id), parent_mailbox_(mailbox) {}

} // namespace ac
