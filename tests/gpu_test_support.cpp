#include <string>

// Standalone GPU integration tests compile selected implementation units
// without c_api.cpp, so they provide the shared C ABI diagnostic storage.
thread_local std::string mkvc_last_error;
