#ifndef CONF_H
#define CONF_H

enum channel_ids {
    PM2_DD = 0,
    PM3_DD = 1,
    PM4_DD = 2,
    PM5_CPU = 3,
    PM6_CPU = 4,
    PM7_CPU = 5,
    CHAN7 = 6,
    CHAN8 = 7
};

#define NI_CHANNELS "Dev1/ai0, Dev1/ai1, Dev1/ai2, Dev1/ai3," \
    "Dev1/ai4, Dev1/ai5, Dev1/ai6, Dev1/ai7"
#define U_MIN ((double)-0.2)
#define U_MAX ((double)0.2)
#define CLK_SRC "OnboardClock"
#define SAMPLING_RATE ((unsigned int)18000)
#define TIMEOUT ((unsigned int)10)

#endif
