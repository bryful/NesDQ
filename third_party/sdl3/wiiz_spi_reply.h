/* Wiiz compatibility check for SDL's existing Switch SPI reader.
 * Local modification; rationale: https://github.com/libsdl-org/SDL/issues/16434
 * No protocol, calibration or input decoding is implemented here.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool wiizSwitchSPIReplyMatches(uint32_t requested_address, size_t requested_length,
                                     uint32_t returned_address, size_t returned_length,
                                     size_t available)
{
    return requested_address == returned_address &&
           returned_length >= requested_length && returned_length <= available;
}
