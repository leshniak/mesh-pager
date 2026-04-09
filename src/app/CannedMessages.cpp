#include "app/CannedMessages.h"
#include "config/AppConfig.h"

#include <Preferences.h>

namespace mesh::app {

static Preferences prefs;

void CannedMessages::init(const char* const* msgs, size_t count) {
    messages_ = msgs;
    count_ = count;

    prefs.begin(config::kAppName, true);
    index_ = prefs.getUChar(config::kMsgIndexKey, 0);
    prefs.end();

    if (index_ >= count_) {
        index_ = 0;
    }
}

std::string_view CannedMessages::current() const {
    if (!messages_ || count_ == 0) return {};
    return messages_[index_];
}

void CannedMessages::next() {
    if (count_ == 0) return;
    index_ = static_cast<uint8_t>((index_ + 1) % count_);
}

void CannedMessages::save() {
    prefs.begin(config::kAppName, false);
    if (index_ != prefs.getUChar(config::kMsgIndexKey, 0xFF)) {
        prefs.putUChar(config::kMsgIndexKey, index_);
    }
    prefs.end();
}

}  // namespace mesh::app
