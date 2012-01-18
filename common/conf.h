/*
 *  Records analog data from a NI USB-6218 and send it to connected clients
 *
 *  Copyright (C)2011-2012, Johannes Weiß <weiss@tux4u.de>
 *                        , Jonathan Dimond <jonny@dimond.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CONF_H
#define CONF_H

enum channel_ids {
    PM7_CPU = 0,
    PM2_DD  = 1,
    PM3_DD  = 2,
    PM4_DD  = 3,
    PM5_CPU = 4,
    PM6_CPU = 5,
    CHAN7   = 6,
    CHAN8   = 7
};

#define NI_CHANNELS "Dev1/ai0, Dev1/ai1, Dev1/ai2, Dev1/ai3," \
    "Dev1/ai4, Dev1/ai5, Dev1/ai6, Dev1/ai7"
#define U_MIN ((double)-0.2)
#define U_MAX ((double)0.2)
#define CLK_SRC "OnboardClock"
#define NI_SAMPLING_RATE ((unsigned int)18000)
#define TIMEOUT ((unsigned int)10)

#endif
/* vim: set fileencoding=utf8 : */
