#ifndef __APDCAM10G_TERMINAL_H__
#define __APDCAM10G_TERMINAL_H__

/*

Default	\033[39m	\033[49m
Black	\033[30m	\033[40m
Dark red	\033[31m	\033[41m
Dark green	\033[32m	\033[42m
Dark yellow (Orange-ish)	\033[33m	\033[43m
Dark blue	\033[34m	\033[44m
Dark magenta	\033[35m	\033[45m
Dark cyan	\033[36m	\033[46m
Light gray	\033[37m	\033[47m
Dark gray	\033[90m	\033[100m
Red	\033[91m	\033[101m
Green	\033[92m	\033[101m
Orange	\033[93m	\033[103m
Blue	\033[94m	\033[104m
Magenta	\033[95m	\033[105m
Cyan	\033[96m	\033[106m
White	\033[97m	\033[107m
 */

namespace apdcam10g
{
    namespace terminal
    {
        static const char *black_fg = "\033[30m";
        static const char *black_bg = "\033[49m";
        static const char *red_fg = "\033[91m";
        static const char *red_bg = "\033[101m";
        static const char *green_fg = "\033[92m";
        static const char *green_bg = "\033[102m";
        static const char *blue_fg = "\033[94m";
        static const char *blue_bg = "\033[104m";
        static const char *orange_fg = "\033[93m";
        static const char *orange_bg = "\033[103m";
        static const char *reset = "\033[0m";
    };
}


#endif
