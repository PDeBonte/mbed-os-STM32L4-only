/*
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SARA4_PPP_H_
#define SARA4_PPP_H_

#include "AT_CellularDevice.h"

namespace mbed {

class SARA4_PPP : public AT_CellularDevice {

public:
    SARA4_PPP(FileHandle *fh);
    virtual ~SARA4_PPP();

public: // CellularDevice
    virtual AT_CellularNetwork *open_network_impl(ATHandler &at);
};

} // namespace mbed

#endif // SARA4_PPP_H_
