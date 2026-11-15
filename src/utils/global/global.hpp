#include "utils/global/errorstatus.hpp"

#define CRITICALMESSAGE                                                                                             \
    std::string critical_error = fmt::format("Failure: {} : {} : {} : {}", e.what(), __func__, __FILE__, __LINE__); \
    Message::CriticalMessage(critical_error);

// Answers 503 (with a short message) when the database is busy, 500 otherwise.
#define CRITICALMESSAGERESPONSE                                                                                              \
    CRITICALMESSAGE                                                                                                          \
    callback(api::v2::Http::statusForException(e),                                                                           \
        api::v2::Http::statusForException(e) == api::v2::Http::Status::SERVICE_UNAVAILABLE ? std::string(e.what()) : critical_error);
