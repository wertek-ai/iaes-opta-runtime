/**
 * RS485 has no meaning on a PC. Present so the runtime compiles; the delays it
 * would set are a property of a transceiver that is not there.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_RS485_H
#define HOST_RS485_H

class HostRS485 {
public:
    void setDelays(unsigned long, unsigned long) {}
};
extern HostRS485 RS485;

#endif  // HOST_RS485_H
