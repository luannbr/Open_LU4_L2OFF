#pragma once

namespace EXCEPTION {

// Original L2Core owner. Only the current shutdown-reachable static leaf is
// published here. It has no object layout or virtual surface.
class CMiniDump {
public:
    static bool DontMakeDumpFile();
};

} // namespace EXCEPTION
