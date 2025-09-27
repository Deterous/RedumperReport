module;

#include <cstdint>

export module common;

namespace gpsxre
{

export enum class State : uint8_t
{
    ERROR_SKIP, // must be first to support random offset file writes
    ERROR_C2,
    SUCCESS_C2_OFF,
    SUCCESS_SCSI_OFF,
    SUCCESS
};

}
