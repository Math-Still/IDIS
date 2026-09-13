// Boost.JSON compiled implementation for standalone Adapter plugin examples.
//
// DeviceAdapter/OsAdapter API v1 exposes Boost.JSON types across the C++ ABI.
// A shared-library plugin therefore must link a Boost.JSON implementation that
// is ABI-compatible with the host. Keeping this in one translation unit avoids
// undefined Boost.JSON symbols when dlopen()/LoadLibrary loads the plugin.
#include <boost/json/src.hpp>
